/*
 *  linux/arch/x86_64/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002  Andi Kleen SuSE Labs
 *  Copyright (C) 2000  Pavel Machek <pavel@suse.cz>
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after an interrupt and after each system call.
 *
 * Normal syscalls and interrupts don't save a full stack frame, this is
 * only done for syscall tracing, signals or fork/exec et.al.
 *
 * A note on terminology:
 * - top of stack: Architecture defined interrupt frame from SS to RIP
 * at the top of the kernel process stack.
 * - partial stack frame: partially saved registers up to R11.
 * - full stack frame: Like partial stack frame, but all register saved.
 *
 * Some macro usage:
 * - CFI macros are used to generate dwarf2 unwind information for better
 * backtraces. They don't change any code.
 * - SAVE_ALL/RESTORE_ALL - Save/restore all registers
 * - SAVE_ARGS/RESTORE_ARGS - Save/restore registers that C functions modify.
 * There are unfortunately lots of special cases where some registers
 * not touched. The macro is a big mess that should be cleaned up.
 * - SAVE_REST/RESTORE_REST - Handle the registers not saved by SAVE_ARGS.
 * Gives a full stack frame.
 * - ENTRY/END Define functions in the symbol table.
 * - FIXUP_TOP_OF_STACK/RESTORE_TOP_OF_STACK - Fix up the hardware stack
 * frame that is otherwise undefined after a SYSCALL
 * - TRACE_IRQ_* - Trace hard interrupt state for lock debugging.
 * - errorentry/paranoidentry/zeroentry - Define exception entry points.
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/dwarf2.h>
#include <asm/calling.h>
#include <asm/asm-offsets.h>
#include <asm/msr.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/hw_irq.h>
#include <asm/page_types.h>
#include <asm/irqflags.h>
#include <asm/paravirt.h>
#include <asm/ftrace.h>
#include <asm/percpu.h>
#include <asm/asm.h>
#include <asm/pgtable_types.h>
#include <asm/cpufeature.h>
#include <asm/kaiser.h>
#include <asm/spec_ctrl.h>
#include <asm/nospec-branch.h>

/* Avoid __ASSEMBLER__'ifying <linux/audit.h> just for this.  */
#include <linux/elf-em.h>
#define AUDIT_ARCH_X86_64	(EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#define __AUDIT_ARCH_64BIT 0x80000000
#define __AUDIT_ARCH_LE	   0x40000000

	.code64
	.section .entry.text, "ax"

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(mcount)
	RET
END(mcount)

ENTRY(ftrace_caller)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	MCOUNT_SAVE_FRAME

	movq 0x38(%rsp), %rdi
	movq 8(%rbp), %rsi
	subq $MCOUNT_INSN_SIZE, %rdi

GLOBAL(ftrace_call)
	call ftrace_stub

	MCOUNT_RESTORE_FRAME

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
GLOBAL(ftrace_graph_call)
	jmp ftrace_stub
#endif

GLOBAL(ftrace_stub)
	RET
END(ftrace_caller)

#else /* ! CONFIG_DYNAMIC_FTRACE */
ENTRY(mcount)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	cmpq $ftrace_stub, ftrace_trace_function
	jnz trace

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpq $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpq $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif

GLOBAL(ftrace_stub)
	RET

trace:
	MCOUNT_SAVE_FRAME

	movq 0x38(%rsp), %rdi
	movq 8(%rbp), %rsi
	subq $MCOUNT_INSN_SIZE, %rdi

	movq ftrace_trace_function, %r8
	CALL_NOSPEC %r8

	MCOUNT_RESTORE_FRAME

	jmp ftrace_stub
END(mcount)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	cmpl $0, function_trace_stop
	jne ftrace_stub

	MCOUNT_SAVE_FRAME

	leaq 8(%rbp), %rdi
	movq 0x38(%rsp), %rsi
	movq (%rbp), %rdx
	subq $MCOUNT_INSN_SIZE, %rsi

	call	prepare_ftrace_return

	MCOUNT_RESTORE_FRAME

	RET
END(ftrace_graph_caller)

GLOBAL(return_to_handler)
	subq  $24, %rsp

	/* Save the return values */
	movq %rax, (%rsp)
	movq %rdx, 8(%rsp)
	movq %rbp, %rdi

	call ftrace_return_to_handler

	movq %rax, %rdi
	movq 8(%rsp), %rdx
	movq (%rsp), %rax
	addq $24, %rsp
	JMP_NOSPEC %rdi
#endif


#ifndef CONFIG_PREEMPT
#define retint_kernel retint_restore_args
#endif

#ifdef CONFIG_PARAVIRT
ENTRY(native_usergs_sysret64)
	swapgs
	CLEAR_CPU_BUFFERS
	sysretq
ENDPROC(native_usergs_sysret64)
#endif /* CONFIG_PARAVIRT */


.macro TRACE_IRQS_IRETQ offset=ARGOFFSET
#ifdef CONFIG_TRACE_IRQFLAGS
	bt   $9,EFLAGS-\offset(%rsp)	/* interrupts off? */
	jnc  1f
	TRACE_IRQS_ON
1:
#endif
.endm

/*
 * C code is not supposed to know about undefined top of stack. Every time
 * a C function with an pt_regs argument is called from the SYSCALL based
 * fast path FIXUP_TOP_OF_STACK is needed.
 * RESTORE_TOP_OF_STACK syncs the syscall state after any possible ptregs
 * manipulation.
 */

	/* %rsp:at FRAMEEND */
	.macro FIXUP_TOP_OF_STACK tmp offset=0
	movq PER_CPU_VAR(old_rsp),\tmp
	movq \tmp,RSP+\offset(%rsp)
	movq $__USER_DS,SS+\offset(%rsp)
	movq $__USER_CS,CS+\offset(%rsp)
	movq $-1,RCX+\offset(%rsp)
	movq R11+\offset(%rsp),\tmp  /* get eflags */
	movq \tmp,EFLAGS+\offset(%rsp)
	.endm

	.macro RESTORE_TOP_OF_STACK tmp offset=0
	movq RSP+\offset(%rsp),\tmp
	movq \tmp,PER_CPU_VAR(old_rsp)
	movq EFLAGS+\offset(%rsp),\tmp
	movq \tmp,R11+\offset(%rsp)
	.endm

	.macro FAKE_STACK_FRAME child_rip
	/* push in order ss, rsp, eflags, cs, rip */
	xorl %eax, %eax
	pushq_cfi $__KERNEL_DS /* ss */
	/*CFI_REL_OFFSET	ss,0*/
	pushq_cfi %rax /* rsp */
	CFI_REL_OFFSET	rsp,0
	pushq_cfi $X86_EFLAGS_IF /* eflags - interrupts on */
	/*CFI_REL_OFFSET	rflags,0*/
	pushq_cfi $__KERNEL_CS /* cs */
	/*CFI_REL_OFFSET	cs,0*/
	pushq_cfi \child_rip /* rip */
	CFI_REL_OFFSET	rip,0
	pushq_cfi %rax /* orig rax */
	.endm

	.macro UNFAKE_STACK_FRAME
	addq $8*6, %rsp
	CFI_ADJUST_CFA_OFFSET	-(6*8)
	.endm

/*
 * initial frame state for interrupts (and exceptions without error code)
 */
	.macro EMPTY_FRAME offset=0
	CFI_STARTPROC simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA rsp,\offset
	.endm

/*
 * initial frame state for interrupts (and exceptions without error code)
 */
	.macro INTR_FRAME start=1 offset=0
	.if \start
	EMPTY_FRAME SS+8+\offset-RIP
	.else
	CFI_DEF_CFA_OFFSET SS+8+\offset-RIP
	.endif
	/*CFI_REL_OFFSET ss, SS+\offset-RIP*/
	CFI_REL_OFFSET rsp, RSP+\offset-RIP
	/*CFI_REL_OFFSET rflags, EFLAGS+\offset-RIP*/
	/*CFI_REL_OFFSET cs, CS+\offset-RIP*/
	CFI_REL_OFFSET rip, RIP+\offset-RIP
	.endm

/*
 * initial frame state for exceptions with error code (and interrupts
 * with vector already pushed)
 */
	.macro XCPT_FRAME start=1 offset=0
	INTR_FRAME \start, RIP+\offset-ORIG_RAX
	.endm

/*
 * frame that enables calling into C.
 */
	.macro PARTIAL_FRAME start=1 offset=0
	.if \start >= 0
	XCPT_FRAME \start, ORIG_RAX+\offset-ARGOFFSET
	.endif
	CFI_REL_OFFSET rdi, RDI+\offset-ARGOFFSET
	CFI_REL_OFFSET rsi, RSI+\offset-ARGOFFSET
	CFI_REL_OFFSET rdx, RDX+\offset-ARGOFFSET
	CFI_REL_OFFSET rcx, RCX+\offset-ARGOFFSET
	CFI_REL_OFFSET rax, RAX+\offset-ARGOFFSET
	CFI_REL_OFFSET r8, R8+\offset-ARGOFFSET
	CFI_REL_OFFSET r9, R9+\offset-ARGOFFSET
	CFI_REL_OFFSET r10, R10+\offset-ARGOFFSET
	CFI_REL_OFFSET r11, R11+\offset-ARGOFFSET
	.endm

/*
 * frame that enables passing a complete pt_regs to a C function.
 */
	.macro DEFAULT_FRAME start=1 offset=0
	.if \start >= -1
	PARTIAL_FRAME \start, R11+\offset-R15
	.endif
	CFI_REL_OFFSET rbx, RBX+\offset
	CFI_REL_OFFSET rbp, RBP+\offset
	CFI_REL_OFFSET r12, R12+\offset
	CFI_REL_OFFSET r13, R13+\offset
	CFI_REL_OFFSET r14, R14+\offset
	CFI_REL_OFFSET r15, R15+\offset
	.endm

/* save partial stack frame */
	.pushsection .kprobes.text, "ax"
ENTRY(save_args)
	XCPT_FRAME offset=ORIG_RAX-RBP+8
	cld
	/*
	 * start from rbp in pt_regs and jump over
	 * return address.
	 */
	movq_cfi rdi, RDI+8-RBP
	movq %rsi, RSI+8-RBP(%rsp)
	movq %rdx, RDX+8-RBP(%rsp)
	movq %rcx, RCX+8-RBP(%rsp)
	movq_cfi rax, RAX+8-RBP
	movq  %r8,  R8+8-RBP(%rsp)
	movq  %r9,  R9+8-RBP(%rsp)
	movq %r10, R10+8-RBP(%rsp)
	movq %r11, R11+8-RBP(%rsp)

	leaq -RBP+8(%rsp),%rdi	/* arg1 for handler */
	movq_cfi rbp, 8		/* push %rbp */
	leaq 8(%rsp), %rbp		/* mov %rsp, %ebp */
	CFI_DEF_CFA_REGISTER rbp
	CFI_ADJUST_CFA_OFFSET -8
	testl $3, CS(%rdi)
	je 1f
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	SWITCH_KERNEL_CR3
	jmp 2f
1:
	FENCE_SWAPGS_KERNEL_ENTRY
2:
	/* Switch to proper stack if we are on trampoline stack */
	movq PER_CPU_VAR(init_tss + TSS_sp0), %rcx
	cmpq %rcx, %rsp
	ja 1f
	leaq -TSS_stack_size(%rcx), %rax
	cmpq %rsp, %rax
	ja 1f
	pushq %rdi
	subq %rsp, %rcx
	movq PER_CPU_VAR(kernel_stack), %rdi
	addq $KERNEL_STACK_OFFSET, %rdi
	subq %rcx, %rdi
	movq %rdi, %rax
	movq %rsp, %rsi
	rep movsb
	movq %rax, %rsp
	popq %rdi
	leaq 8(%rsp), %rbp      /* Update rbp to the new value */

	ENABLE_IBRS
	/*
	 * irq_count is used to check if a CPU is already on an interrupt stack
	 * or not. While this is essentially redundant with preempt_count it is
	 * a little cheaper to use a separate counter in the PDA (short of
	 * moving irq_enter into assembly, which would be too much work)
	 */
1:	incl PER_CPU_VAR(irq_count)
	jne 2f
	popq %rax			/* move return address... */
	mov PER_CPU_VAR(irq_stack_ptr),%rsp
	pushq %rbp			/* backlink for unwinder */
	pushq %rax			/* ... to the new stack */
	/*
	 * We entered an interrupt context - irqs are off:
	 */
2:	TRACE_IRQS_OFF
	RET
	CFI_ENDPROC
END(save_args)
	.popsection

ENTRY(save_rest)
	CFI_STARTPROC
	movq 5*8+16(%rsp), %r11	/* save return address */
	movq %rbx, RBX+16(%rsp)
	movq %rbp, RBP+16(%rsp)
	movq %r12, R12+16(%rsp)
	movq %r13, R13+16(%rsp)
	movq %r14, R14+16(%rsp)
	movq %r15, R15+16(%rsp)
	movq %r11, 8(%rsp)	/* return address */
	FIXUP_TOP_OF_STACK %r11, 16
	RET
	CFI_ENDPROC
END(save_rest)

/* save complete stack frame */
	.pushsection .kprobes.text, "ax"
ENTRY(save_paranoid)
	XCPT_FRAME 1 RDI+8
	cld
	movq_cfi rdi, RDI+8
	movq_cfi rsi, RSI+8
	movq_cfi rdx, RDX+8
	movq_cfi rcx, RCX+8
	movq_cfi rax, RAX+8
	movq_cfi r8, R8+8
	movq_cfi r9, R9+8
	movq_cfi r10, R10+8
	movq_cfi r11, R11+8
	movq_cfi rbx, RBX+8
	movq_cfi rbp, RBP+8
	movq_cfi r12, R12+8
	movq_cfi r13, R13+8
	movq_cfi r14, R14+8
	movq_cfi r15, R15+8
	movl $1,%ebx
	movl $MSR_GS_BASE,%ecx
	rdmsr
	testl %edx,%edx
	js 1f	/* negative -> in kernel */
	SWAPGS
	xorl %ebx,%ebx
1:
	SAVE_AND_SWITCH_TO_KERNEL_CR3 scratch_reg=%rax save_reg=%r14
	FENCE_SWAPGS_KERNEL_ENTRY
	ENABLE_IBRS
	RET
	CFI_ENDPROC
END(save_paranoid)
	.popsection

/*
 * A newly forked process directly context switches into this address.
 *
 * rdi: prev task we switched from
 */
ENTRY(ret_from_fork)
	DEFAULT_FRAME

	LOCK ; btr $TIF_FORK,TI_flags(%r8)

	pushq_cfi kernel_eflags(%rip)
	popfq_cfi				# reset kernel eflags

	call schedule_tail			# rdi: 'prev' task parameter

	GET_THREAD_INFO(%rcx)

	RESTORE_REST

	testl $3, CS-ARGOFFSET(%rsp)		# from kernel_thread?
	je   int_ret_from_sys_call

	/*
	 * By the time we get here, we have no idea whether our pt_regs,
	 * ti flags, and ti status came from the 64-bit SYSCALL fast path,
	 * the slow path, or one of the ia32entry paths.
	 * Use int_ret_from_sys_call to return, since it can safely handle
	 * all of the above.
	 */
	jmp  int_ret_from_sys_call

	CFI_ENDPROC
END(ret_from_fork)

/*
 * System call entry. Up to 6 arguments in registers are supported.
 *
 * SYSCALL does not save anything on the stack and does not change the
 * stack pointer.
 */

/*
 * Register setup:
 * rax  system call number
 * rdi  arg0
 * rcx  return address for syscall/sysret, C arg3
 * rsi  arg1
 * rdx  arg2
 * r10  arg3 	(--> moved to rcx for C)
 * r8   arg4
 * r9   arg5
 * r11  eflags for syscall/sysret, temporary for C
 * r12-r15,rbp,rbx saved by C code, not touched.
 *
 * Interrupts are off on entry.
 * Only called from user space.
 *
 * XXX	if we had a free scratch register we could save the RSP into the stack frame
 *      and report it properly in ps. Unfortunately we haven't.
 *
 * When user can change the frames always force IRET. That is because
 * it deals with uncanonical addresses better. SYSRET has trouble
 * with them due to bugs in both AMD and Intel CPUs.
 */

ENTRY(system_call)
	CFI_STARTPROC	simple
	CFI_SIGNAL_FRAME
	CFI_DEF_CFA	rsp,KERNEL_STACK_OFFSET
	CFI_REGISTER	rip,rcx
	/*CFI_REGISTER	rflags,r11*/
	SWAPGS_UNSAFE_STACK
	SWITCH_KERNEL_CR3_NO_STACK
	/*
	 * A hypervisor implementation might want to use a label
	 * after the swapgs, so that it can do the swapgs
	 * for the guest and jump here on syscall.
	 */
ENTRY(system_call_after_swapgs)

	movq	%rsp,PER_CPU_VAR(old_rsp)
	movq	PER_CPU_VAR(kernel_stack),%rsp
	/*
	 * No need to follow this irqs off/on section - it's straight
	 * and short:
	 */
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_ARGS 8,1
	movq  %rax,ORIG_RAX-ARGOFFSET(%rsp)
	movq  %rcx,RIP-ARGOFFSET(%rsp)
	CFI_REL_OFFSET rip,RIP-ARGOFFSET
	GET_THREAD_INFO(%rcx)

	ENABLE_IBRS

	testl $_TIF_WORK_SYSCALL_ENTRY,TI_flags(%rcx)
	jnz tracesys
system_call_fastpath:
	cmpq $NR_syscalls,%rax
	jae badsys
	sbbq %rcx,%rcx			/* array_index_mask_nospec() */
	andq %rcx,%rax
	movq %r10,%rcx
#ifdef CONFIG_RETPOLINE
	movq    sys_call_table(, %rax, 8), %rax
	call    __x86_indirect_thunk_rax
#else
	call *sys_call_table(,%rax,8)  # XXX:	 rip relative
#endif
	movq %rax,RAX-ARGOFFSET(%rsp)
/*
 * Syscall return path ending with SYSRET (fast path)
 * Has incomplete stack frame and undefined top of stack.
 */
ret_from_sys_call:
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	flagmask */
sysret_check:
	LOCKDEP_SYS_EXIT
	GET_THREAD_INFO(%rcx)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	jnz  sysret_careful
	CFI_REMEMBER_STATE
	/*
	 * sysretq will re-enable interrupts:
	 */
	TRACE_IRQS_ON
	movq RIP-ARGOFFSET(%rsp),%rcx
	CFI_REGISTER	rip,rcx
	RESTORE_ARGS 1,-ARG_SKIP,0
	/*CFI_REGISTER	rflags,r11*/

	call amd_clear_divider
	DISABLE_IBRS
	/*
	 * This opens a window where we have a user CR3, but are
	 * running in the kernel.  This makes using the CS
	 * register useless for telling whether or not we need to
	 * switch CR3 in NMIs.  Normal interrupts are OK because
	 * they are off here.
	 */
	movq	PER_CPU_VAR(old_rsp), %rsp
	SWITCH_USER_CR3_NO_STACK
	USERGS_SYSRET64

	CFI_RESTORE_STATE
	/* Handle reschedules */
	/* edx:	work, edi: workmask */
sysret_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc sysret_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	call schedule
	popq_cfi %rdi
	jmp sysret_check

	/* Handle a signal */
sysret_signal:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
#ifdef CONFIG_AUDITSYSCALL
	bt $TIF_SYSCALL_AUDIT,%edx
	jc sysret_audit
#endif
	/*
	 * We have a signal, or exit tracing or single-step.
	 * These all wind up with the iret return path anyway,
	 * so just join that path right now.
	 */
	FIXUP_TOP_OF_STACK %r11, -ARGOFFSET
	jmp int_check_syscall_exit_work

badsys:
	movq $-ENOSYS,RAX-ARGOFFSET(%rsp)
	jmp ret_from_sys_call

#ifdef CONFIG_AUDITSYSCALL
	/*
	 * Fast path for syscall audit without full syscall trace.
	 * We just call audit_syscall_entry() directly, and then
	 * jump back to the normal fast path.
	 */
auditsys:
	movq %r10,%r9			/* 6th arg: 4th syscall arg */
	movq %rdx,%r8			/* 5th arg: 3rd syscall arg */
	movq %rsi,%rcx			/* 4th arg: 2nd syscall arg */
	movq %rdi,%rdx			/* 3rd arg: 1st syscall arg */
	movq %rax,%rsi			/* 2nd arg: syscall number */
	movl $AUDIT_ARCH_X86_64,%edi	/* 1st arg: audit arch */
	call audit_syscall_entry
	LOAD_ARGS 0		/* reload call-clobbered registers */
	jmp system_call_fastpath

	/*
	 * Return fast path for syscall audit.  Call audit_syscall_exit()
	 * directly and then jump back to the fast path with TIF_SYSCALL_AUDIT
	 * masked off.
	 */
sysret_audit:
	movq RAX-ARGOFFSET(%rsp),%rsi	/* second arg, syscall return value */
	cmpq $0,%rsi		/* is it < 0? */
	setl %al		/* 1 if so, 0 if not */
	movzbl %al,%edi		/* zero-extend that into %edi */
	inc %edi /* first arg, 0->1(AUDITSC_SUCCESS), 1->2(AUDITSC_FAILURE) */
	call audit_syscall_exit
	movl $(_TIF_ALLWORK_MASK & ~_TIF_SYSCALL_AUDIT),%edi
	jmp sysret_check
#endif	/* CONFIG_AUDITSYSCALL */

	/* Do syscall tracing */
tracesys:
#ifdef CONFIG_AUDITSYSCALL
	testl $(_TIF_WORK_SYSCALL_ENTRY & ~_TIF_SYSCALL_AUDIT),TI_flags(%rcx)
	jz auditsys
#endif
	SAVE_REST
	movq $-ENOSYS,RAX(%rsp) /* ptrace can change this for a bad syscall */
	FIXUP_TOP_OF_STACK %rdi
	movq %rsp,%rdi
	call syscall_trace_enter
	/*
	 * Reload arg registers from stack in case ptrace changed them.
	 * We don't reload %rax because syscall_trace_enter() returned
	 * the value it wants us to use in the table lookup.
	 */
	LOAD_ARGS ARGOFFSET, 1
	RESTORE_REST
	cmpq $NR_syscalls,%rax
	jae  int_ret_from_sys_call	/* RAX(%rsp) set to -ENOSYS above */
	sbbq %rcx,%rcx			/* array_index_mask_nospec() */
	andq %rcx,%rax
	movq %r10,%rcx	/* fixup for C */
#ifdef CONFIG_RETPOLINE
	movq    sys_call_table(, %rax, 8), %rax
	call    __x86_indirect_thunk_rax
#else
	call *sys_call_table(,%rax,8)
#endif
	movq %rax,RAX-ARGOFFSET(%rsp)
	/* Use IRET because user could have changed frame */

/*
 * Syscall return path ending with IRET.
 * Has correct top of stack, but partial stack frame.
 */
GLOBAL(int_ret_from_sys_call)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_restore_args
	movl $_TIF_ALLWORK_MASK,%edi
	/* edi:	mask to check */
GLOBAL(int_with_check)
	LOCKDEP_SYS_EXIT_IRQ
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	jnz   int_careful
	andl    $~TS_COMPAT,TI_status(%rcx)
	jmp   retint_swapgs

	/* Either reschedule or signal or syscall exit tracking needed. */
	/* First do a reschedule test. */
	/* edx:	work, edi: workmask */
int_careful:
	bt $TIF_NEED_RESCHED,%edx
	jnc  int_very_careful
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	call schedule
	popq_cfi %rdi
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check

	/* handle signals and tracing -- both require a full stack frame */
int_very_careful:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
int_check_syscall_exit_work:
	SAVE_REST
	/* Check for syscall exit trace */
	testl $_TIF_WORK_SYSCALL_EXIT,%edx
	jz int_signal
	pushq_cfi %rdi
	leaq 8(%rsp),%rdi	# &ptregs -> arg1
	call syscall_trace_leave
	popq_cfi %rdi
	andl $~(_TIF_WORK_SYSCALL_EXIT|_TIF_SYSCALL_EMU),%edi
	jmp int_restore_rest

int_signal:
	testl $_TIF_DO_NOTIFY_MASK,%edx
	jz 1f
	movq %rsp,%rdi		# &ptregs -> arg1
	xorl %esi,%esi		# oldset -> arg2
	call do_notify_resume
1:	movl $_TIF_WORK_MASK,%edi
int_restore_rest:
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp int_with_check
	CFI_ENDPROC
END(system_call)

/*
 * Certain special system calls that need to save a complete full stack frame.
 */
	.macro PTREGSCALL label,func,arg
ENTRY(\label)
	PARTIAL_FRAME 1 8		/* offset 8: return address */
	subq $REST_SKIP, %rsp
	CFI_ADJUST_CFA_OFFSET REST_SKIP
	call save_rest
	DEFAULT_FRAME -2 8		/* offset 8: return address */
	leaq 8(%rsp), \arg	/* pt_regs pointer */
	call \func
	jmp ptregscall_common
	CFI_ENDPROC
END(\label)
	.endm

	PTREGSCALL stub_clone, sys_clone, %r8
	PTREGSCALL stub_fork, sys_fork, %rdi
	PTREGSCALL stub_vfork, sys_vfork, %rdi
	PTREGSCALL stub_sigaltstack, sys_sigaltstack, %rdx
	PTREGSCALL stub_iopl, sys_iopl, %rsi

ENTRY(ptregscall_common)
	DEFAULT_FRAME 1 8	/* offset 8: return address */
	RESTORE_TOP_OF_STACK %r11, 8
	movq_cfi_restore R15+8, r15
	movq_cfi_restore R14+8, r14
	movq_cfi_restore R13+8, r13
	movq_cfi_restore R12+8, r12
	movq_cfi_restore RBP+8, rbp
	movq_cfi_restore RBX+8, rbx
	ret $REST_SKIP		/* pop extended registers */
	CFI_ENDPROC
END(ptregscall_common)

ENTRY(stub_execve)
	CFI_STARTPROC

	ENABLE_IBRS

	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	FIXUP_TOP_OF_STACK %r11
	movq %rsp, %rcx
	call sys_execve
	RESTORE_TOP_OF_STACK %r11
	movq %rax,RAX(%rsp)
	RESTORE_REST

	DISABLE_IBRS

	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_execve)

/*
 * sigreturn is special because it needs to restore all registers on return.
 * This cannot be done with SYSRET, so use the IRET return path instead.
 */
ENTRY(stub_rt_sigreturn)
	CFI_STARTPROC

	ENABLE_IBRS

	addq $8, %rsp
	PARTIAL_FRAME 0
	SAVE_REST
	movq %rsp,%rdi
	FIXUP_TOP_OF_STACK %r11
	call sys_rt_sigreturn
	movq %rax,RAX(%rsp) # fixme, this could be done at the higher layer
	RESTORE_REST

	DISABLE_IBRS

	jmp int_ret_from_sys_call
	CFI_ENDPROC
END(stub_rt_sigreturn)

/*
 * Build the entry stubs and pointer table with some assembler magic.
 * We pack 7 stubs into a single 32-byte chunk, which will fit in a
 * single cache line on all modern x86 implementations.
 */
	.section .init.rodata,"a"
ENTRY(interrupt)
	.section .entry.text
	.p2align 5
	.p2align CONFIG_X86_L1_CACHE_SHIFT
ENTRY(irq_entries_start)
	INTR_FRAME
vector=FIRST_EXTERNAL_VECTOR
.rept (NR_VECTORS-FIRST_EXTERNAL_VECTOR+6)/7
	.balign 32
  .rept	7
    .if vector < NR_VECTORS
      .if vector <> FIRST_EXTERNAL_VECTOR
	CFI_ADJUST_CFA_OFFSET -8
      .endif
1:	pushq_cfi $(~vector+0x80)	/* Note: always in signed byte range */
      .if ((vector-FIRST_EXTERNAL_VECTOR)%7) <> 6
	jmp 2f
      .endif
      .previous
	.quad 1b
      .section .entry.text
vector=vector+1
    .endif
  .endr
2:	jmp common_interrupt
.endr
	CFI_ENDPROC
END(irq_entries_start)

.previous
END(interrupt)
.previous

/*
 * Interrupt entry/exit.
 *
 * Interrupt entry points save only callee clobbered registers in fast path.
 *
 * Entry runs with interrupts off.
 */

/* 0(%rsp): ~(interrupt number) */
	.macro interrupt func
	/* reserve pt_regs for scratch regs and rbp */
	subq $ORIG_RAX-RBP, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-RBP
	call save_args
	PARTIAL_FRAME -1 ARGOFFSET-RBP
	CFI_REL_OFFSET rbp, 0
	CFI_DEF_CFA_REGISTER rbp
	call \func
	.endm

/*
 * Interrupt entry/exit should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"
	/*
	 * The interrupt stubs push (~vector+0x80) onto the stack and
	 * then jump to common_interrupt.
	 */
	.p2align CONFIG_X86_L1_CACHE_SHIFT
common_interrupt:
	XCPT_FRAME
	addq $-0x80,(%rsp)		/* Adjust vector to [-256,-1] range */
	interrupt do_IRQ
	/* 0(%rsp): old_rsp-ARGOFFSET */
ret_from_intr:
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	decl PER_CPU_VAR(irq_count)
	leaveq
	CFI_RESTORE		rbp
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET	-8

	/* we did not save rbx, restore only from ARGOFFSET */
	addq $8, %rsp
	CFI_ADJUST_CFA_OFFSET	-8
exit_intr:
	GET_THREAD_INFO(%rcx)
	testl $3,CS-ARGOFFSET(%rsp)
	je retint_kernel

	/* Interrupt came from user space */
	/*
	 * Has a correct top of stack, but a partial stack frame
	 * %rcx: thread info. Interrupts off.
	 */
retint_with_reschedule:
	movl $_TIF_WORK_MASK,%edi
retint_check:
	LOCKDEP_SYS_EXIT_IRQ
	movl TI_flags(%rcx),%edx
	andl %edi,%edx
	CFI_REMEMBER_STATE
	jnz  retint_careful

retint_swapgs:		/* return to user-space */
	/*
	 * The iretq could re-enable interrupts:
	 */
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_IRETQ
	/*
	 * This opens a window where we have a user CR3, but are
	 * running in the kernel.  This makes using the CS
	 * register useless for telling whether or not we need to
	 * switch CR3 in NMIs.  Normal interrupts are OK because
	 * they are off here.
	 */

swapgs_restore_regs_and_return_to_usermode:
	call amd_clear_divider
	DISABLE_IBRS
	/*
	 * Returning from an interrupt/NMI/exception to user space, currently
	 * on the kernel task stack, which is not user-mapped.  Copy the iret
	 * frame to the trampoline stack and switch to it before returning.
	 */
	RESTORE_ARGS 1,8,1
	pushq %rax
	movq %rsp, %rax
	movq PER_CPU_VAR(init_tss + TSS_sp0), %rsp
	pushq SS-ORIG_RAX(%rax)
	pushq RSP-ORIG_RAX(%rax)
	pushq EFLAGS-ORIG_RAX(%rax)
	pushq CS-ORIG_RAX(%rax)
	pushq RIP-ORIG_RAX(%rax)
	movq (%rax), %rax
	SWITCH_USER_CR3_NO_STACK
	SWAPGS
	CLEAR_CPU_BUFFERS
	jmp irq_return

paranoid_userspace_restore_all:
	TRACE_IRQS_IRETQ
	RESTORE_REST
	jmp swapgs_restore_regs_and_return_to_usermode

retint_restore_args:	/* return to kernel space */
	DISABLE_INTERRUPTS(CLBR_ANY)
	/*
	 * The iretq could re-enable interrupts:
	 */
	TRACE_IRQS_IRETQ
	RESTORE_ARGS 1,8,1

irq_return:
	INTERRUPT_RETURN

ENTRY(native_iret)
	/*
	 * Are we returning to a stack segment from the LDT?  Note: in
	 * 64-bit mode SS:RSP on the exception stack is always valid.
	 */
#ifdef CONFIG_X86_ESPFIX64
	testb $4,(SS-RIP)(%rsp)
	jnz native_irq_return_ldt
#endif

.global native_irq_return_iret
native_irq_return_iret:
	/*
	 * This may fault.  Non-paranoid faults on return to userspace are
	 * handled by fixup_bad_iret.  These include #SS, #GP, and #NP.
	 * Double-faults due to espfix64 are handled in do_double_fault.
	 * Other faults here are fatal.
	 */
	iretq

#ifdef CONFIG_X86_ESPFIX64
native_irq_return_ldt:
	pushq_cfi %rax
	pushq_cfi %rdi
	SWAPGS
	SWITCH_KERNEL_CR3

	ENABLE_IBRS

	movq PER_CPU_VAR(espfix_waddr),%rdi
	movq %rax,(0*8)(%rdi)	/* RAX */
	movq (2*8)(%rsp),%rax	/* RIP */
	movq %rax,(1*8)(%rdi)
	movq (3*8)(%rsp),%rax	/* CS */
	movq %rax,(2*8)(%rdi)
	movq (4*8)(%rsp),%rax	/* RFLAGS */
	movq %rax,(3*8)(%rdi)
	movq (6*8)(%rsp),%rax	/* SS */
	movq %rax,(5*8)(%rdi)
	movq (5*8)(%rsp),%rax	/* RSP */
	movq %rax,(4*8)(%rdi)
	andl $0xffff0000,%eax
	popq_cfi %rdi
	orq PER_CPU_VAR(espfix_stack),%rax

	call amd_clear_divider
	DISABLE_IBRS

	SWITCH_USER_CR3
	SWAPGS
	movq %rax,%rsp
	popq_cfi %rax
	CLEAR_CPU_BUFFERS
	jmp native_irq_return_iret
#endif

	/* edi: workmask, edx: work */
retint_careful:
	CFI_RESTORE_STATE
	bt    $TIF_NEED_RESCHED,%edx
	jnc   retint_signal
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	pushq_cfi %rdi
	call  schedule
	popq_cfi %rdi
	GET_THREAD_INFO(%rcx)
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp retint_check

retint_signal:
	testl $_TIF_DO_NOTIFY_MASK,%edx
	jz    retint_swapgs
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	SAVE_REST
	movq $-1,ORIG_RAX(%rsp)
	xorl %esi,%esi		# oldset
	movq %rsp,%rdi		# &pt_regs
	call do_notify_resume
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	jmp retint_with_reschedule

#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption */
	/* rcx:	 threadinfo. interrupts off. */
ENTRY(retint_kernel)
	cmpl $0,TI_preempt_count(%rcx)
	jnz  retint_restore_args
	bt  $TIF_NEED_RESCHED,TI_flags(%rcx)
	jnc  retint_restore_args
	bt   $9,EFLAGS-ARGOFFSET(%rsp)	/* interrupts off? */
	jnc  retint_restore_args
	call preempt_schedule_irq
	jmp exit_intr
#endif
	CFI_ENDPROC
END(common_interrupt)

/*
 * End of kprobes section
 */
       .popsection

/*
 * APIC interrupts.
 */
.macro apicinterrupt num sym do_sym
ENTRY(\sym)
	INTR_FRAME
	pushq_cfi $~(\num)
	interrupt \do_sym
	jmp ret_from_intr
	CFI_ENDPROC
END(\sym)
.endm

#ifdef CONFIG_SMP
apicinterrupt IRQ_MOVE_CLEANUP_VECTOR \
	irq_move_cleanup_interrupt smp_irq_move_cleanup_interrupt
apicinterrupt REBOOT_VECTOR \
	reboot_interrupt smp_reboot_interrupt
#endif

#ifdef CONFIG_X86_UV
apicinterrupt UV_BAU_MESSAGE \
	uv_bau_message_intr1 uv_bau_message_interrupt
#endif
apicinterrupt LOCAL_TIMER_VECTOR \
	apic_timer_interrupt smp_apic_timer_interrupt
apicinterrupt X86_PLATFORM_IPI_VECTOR \
	x86_platform_ipi smp_x86_platform_ipi

#ifdef CONFIG_SMP
.irp idx,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, \
	16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
.if NUM_INVALIDATE_TLB_VECTORS > \idx
apicinterrupt (INVALIDATE_TLB_VECTOR_START)+\idx \
	invalidate_interrupt\idx smp_invalidate_interrupt
.endif
.endr
#endif

#ifdef CONFIG_HAVE_KVM
apicinterrupt POSTED_INTR_VECTOR \
	kvm_posted_intr_ipi smp_kvm_posted_intr_ipi
#endif

apicinterrupt THRESHOLD_APIC_VECTOR \
	threshold_interrupt smp_threshold_interrupt
apicinterrupt THERMAL_APIC_VECTOR \
	thermal_interrupt smp_thermal_interrupt

#ifdef CONFIG_X86_MCE
apicinterrupt MCE_SELF_VECTOR \
	mce_self_interrupt smp_mce_self_interrupt
#endif

#ifdef CONFIG_SMP
apicinterrupt CALL_FUNCTION_SINGLE_VECTOR \
	call_function_single_interrupt smp_call_function_single_interrupt
apicinterrupt CALL_FUNCTION_VECTOR \
	call_function_interrupt smp_call_function_interrupt
apicinterrupt RESCHEDULE_VECTOR \
	reschedule_interrupt smp_reschedule_interrupt
#endif

apicinterrupt ERROR_APIC_VECTOR \
	error_interrupt smp_error_interrupt
apicinterrupt SPURIOUS_APIC_VECTOR \
	spurious_interrupt smp_spurious_interrupt

#ifdef CONFIG_IRQ_WORK
apicinterrupt IRQ_WORK_VECTOR \
	irq_work_interrupt smp_irq_work_interrupt
#endif

/*
 * Exception entry points.
 */
.macro zeroentry sym do_sym
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call error_entry
	DEFAULT_FRAME -1
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	call \do_sym
	jmp error_exit		/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro paranoidzeroentry sym do_sym
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	DEFAULT_FRAME -1
	TRACE_IRQS_OFF
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	call \do_sym
	jmp paranoid_exit	/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

#define INIT_TSS_IST(x) PER_CPU_VAR(init_tss) + (TSS_ist + ((x) - 1) * 8)
.macro paranoidzeroentry_ist sym do_sym ist
ENTRY(\sym)
	INTR_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	pushq_cfi $-1		/* ORIG_RAX: no syscall to restart */
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	DEFAULT_FRAME -1
	TRACE_IRQS_OFF
	movq %rsp,%rdi		/* pt_regs pointer */
	xorl %esi,%esi		/* no error code */
	subq $EXCEPTION_STKSZ, INIT_TSS_IST(\ist)
	call \do_sym
	addq $EXCEPTION_STKSZ, INIT_TSS_IST(\ist)
	jmp paranoid_exit	/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

.macro errorentry sym do_sym
ENTRY(\sym)
	XCPT_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call error_entry
	DEFAULT_FRAME -1
	movq %rsp,%rdi			/* pt_regs pointer */
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	call \do_sym
	jmp error_exit			/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

	/* error code is on the stack already */
.macro paranoiderrorentry sym do_sym
ENTRY(\sym)
	XCPT_FRAME
	PARAVIRT_ADJUST_EXCEPTION_FRAME
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	DEFAULT_FRAME -1
	TRACE_IRQS_OFF
	movq %rsp,%rdi			/* pt_regs pointer */
	movq ORIG_RAX(%rsp),%rsi	/* get error code */
	movq $-1,ORIG_RAX(%rsp)		/* no syscall to restart */
	call \do_sym
	jmp paranoid_exit		/* %ebx: no swapgs flag */
	CFI_ENDPROC
END(\sym)
.endm

zeroentry divide_error do_divide_error
zeroentry overflow do_overflow
zeroentry bounds do_bounds
zeroentry invalid_op do_invalid_op
zeroentry device_not_available do_device_not_available
paranoiderrorentry double_fault do_double_fault
zeroentry coprocessor_segment_overrun do_coprocessor_segment_overrun
errorentry invalid_TSS do_invalid_TSS
errorentry segment_not_present do_segment_not_present
zeroentry spurious_interrupt_bug do_spurious_interrupt_bug
zeroentry coprocessor_error do_coprocessor_error
errorentry alignment_check do_alignment_check
zeroentry simd_coprocessor_error do_simd_coprocessor_error

	/* Reload gs selector with exception handling */
	/* edi:  new selector */
ENTRY(native_load_gs_index)
	CFI_STARTPROC
	pushfq_cfi
	DISABLE_INTERRUPTS(CLBR_ANY & ~CLBR_RDI)
	SWAPGS
gs_change:
	movl %edi,%gs
2:	mfence		/* workaround */
	SWAPGS
	popfq_cfi
	ret
	CFI_ENDPROC
END(native_load_gs_index)

	_ASM_EXTABLE(gs_change,bad_gs)
	.section .fixup,"ax"
	/* running with kernelgs */
bad_gs:
	SWAPGS			/* switch back to user gs */
	xorl %eax,%eax
	movl %eax,%gs
	jmp  2b
	.previous

ENTRY(kernel_thread_helper)
	pushq $0		# fake return address
	CFI_STARTPROC
	/*
	 * Here we are in the child and the registers are set as they were
	 * at kernel_thread() invocation in the parent.
	 */
	CALL_NOSPEC %rsi
	# exit
	mov %eax, %edi
	call do_exit
	ud2			# padding for call trace
	CFI_ENDPROC
END(kernel_thread_helper)

/*
 * execve(). This function needs to use IRET, not SYSRET, to set up all state properly.
 *
 * C extern interface:
 *	 extern long execve(const char *name, char **argv, char **envp)
 *
 * asm input arguments:
 *	rdi: name, rsi: argv, rdx: envp
 *
 * We want to fallback into:
 *	extern long sys_execve(const char *name, char **argv,char **envp, struct pt_regs *regs)
 *
 * do_sys_execve asm fallback arguments:
 *	rdi: name, rsi: argv, rdx: envp, rcx: fake frame on the stack
 */
ENTRY(kernel_execve)
	CFI_STARTPROC
	FAKE_STACK_FRAME $0
	SAVE_ALL
	movq %rsp,%rcx
	call sys_execve
	movq %rax, RAX(%rsp)
	RESTORE_REST
	testq %rax,%rax
	je int_ret_from_sys_call
	RESTORE_ARGS
	UNFAKE_STACK_FRAME
	ret
	CFI_ENDPROC
END(kernel_execve)

/* Call softirq on interrupt stack. Interrupts are off. */
ENTRY(call_softirq)
	CFI_STARTPROC
	pushq_cfi %rbp
	CFI_REL_OFFSET rbp,0
	mov  %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	incl PER_CPU_VAR(irq_count)
	cmove PER_CPU_VAR(irq_stack_ptr),%rsp
	push  %rbp			# backlink for old unwinder
	call __do_softirq
	leaveq
	CFI_RESTORE		rbp
	CFI_DEF_CFA_REGISTER	rsp
	CFI_ADJUST_CFA_OFFSET   -8
	decl PER_CPU_VAR(irq_count)
	ret
	CFI_ENDPROC
END(call_softirq)

#ifdef CONFIG_STACK_UNWIND
ENTRY(arch_unwind_init_running)
	CFI_STARTPROC
	movq	%r15, R15(%rdi)
	movq	%r14, R14(%rdi)
	xchgq	%rsi, %rdx
	movq	%r13, R13(%rdi)
	movq	%r12, R12(%rdi)
	xorl	%eax, %eax
	movq	%rbp, RBP(%rdi)
	movq	%rbx, RBX(%rdi)
	movq	(%rsp), %r9
	xchgq	%rdx, %rcx
	movq	%rax, R11(%rdi)
	movq	%rax, R10(%rdi)
	movq	%rax, R9(%rdi)
	movq	%rax, R8(%rdi)
	movq	%rax, RAX(%rdi)
	movq	%rax, RCX(%rdi)
	movq	%rax, RDX(%rdi)
	movq	%rax, RSI(%rdi)
	movq	%rax, RDI(%rdi)
	movq	%rax, ORIG_RAX(%rdi)
	movq	%r9, RIP(%rdi)
	leaq	8(%rsp), %r9
	movq	$__KERNEL_CS, CS(%rdi)
	movq	%rax, EFLAGS(%rdi)
	movq	%r9, RSP(%rdi)
	movq	$__KERNEL_DS, SS(%rdi)
	JMP_NOSPEC %rcx
	CFI_ENDPROC
END(arch_unwind_init_running)
#endif

#ifdef CONFIG_PARAVIRT_XEN
zeroentry xen_hypervisor_callback xen_do_hypervisor_callback

/*
 * A note on the "critical region" in our callback handler.
 * We want to avoid stacking callback handlers due to events occurring
 * during handling of the last event. To do this, we keep events disabled
 * until we've done all processing. HOWEVER, we must enable events before
 * popping the stack frame (can't be done atomically) and so it would still
 * be possible to get enough handler activations to overflow the stack.
 * Although unlikely, bugs of that kind are hard to track down, so we'd
 * like to avoid the possibility.
 * So, on entry to the handler we detect whether we interrupted an
 * existing activation in its critical region -- if so, we pop the current
 * activation and restart the handler using the previous one.
 */
ENTRY(xen_do_hypervisor_callback)   # do_hypervisor_callback(struct *pt_regs)
	CFI_STARTPROC
/*
 * Since we don't modify %rdi, evtchn_do_upall(struct *pt_regs) will
 * see the correct pointer to the pt_regs
 */
	movq %rdi, %rsp            # we don't return, adjust the stack frame
	CFI_ENDPROC
	DEFAULT_FRAME
11:	incl PER_CPU_VAR(irq_count)
	movq %rsp,%rbp
	CFI_DEF_CFA_REGISTER rbp
	cmovzq PER_CPU_VAR(irq_stack_ptr),%rsp
	pushq %rbp			# backlink for old unwinder
	call xen_evtchn_do_upcall
	popq %rsp
	CFI_DEF_CFA_REGISTER rsp
	decl PER_CPU_VAR(irq_count)
	jmp  error_exit
	CFI_ENDPROC
END(xen_do_hypervisor_callback)

/*
 * Hypervisor uses this for application faults while it executes.
 * We get here for two reasons:
 *  1. Fault while reloading DS, ES, FS or GS
 *  2. Fault while executing IRET
 * Category 1 we do not need to fix up as Xen has already reloaded all segment
 * registers that could be reloaded and zeroed the others.
 * Category 2 we fix up by killing the current process. We cannot use the
 * normal Linux return path in this case because if we use the IRET hypercall
 * to pop the stack frame we end up in an infinite loop of failsafe callbacks.
 * We distinguish between categories by comparing each saved segment register
 * with its current contents: any discrepancy means we in category 1.
 */
ENTRY(xen_failsafe_callback)
	INTR_FRAME 1 (6*8)
	/*CFI_REL_OFFSET gs,GS*/
	/*CFI_REL_OFFSET fs,FS*/
	/*CFI_REL_OFFSET es,ES*/
	/*CFI_REL_OFFSET ds,DS*/
	CFI_REL_OFFSET r11,8
	CFI_REL_OFFSET rcx,0
	movw %ds,%cx
	cmpw %cx,0x10(%rsp)
	CFI_REMEMBER_STATE
	jne 1f
	movw %es,%cx
	cmpw %cx,0x18(%rsp)
	jne 1f
	movw %fs,%cx
	cmpw %cx,0x20(%rsp)
	jne 1f
	movw %gs,%cx
	cmpw %cx,0x28(%rsp)
	jne 1f
	/* All segments match their saved values => Category 2 (Bad IRET). */
	movq (%rsp),%rcx
	CFI_RESTORE rcx
	movq 8(%rsp),%r11
	CFI_RESTORE r11
	addq $0x30,%rsp
	CFI_ADJUST_CFA_OFFSET -0x30
	pushq_cfi $0	/* RIP */
	pushq_cfi %r11
	pushq_cfi %rcx
	jmp general_protection
	CFI_RESTORE_STATE
1:	/* Segment mismatch => Category 1 (Bad segment). Retry the IRET. */
	movq (%rsp),%rcx
	CFI_RESTORE rcx
	movq 8(%rsp),%r11
	CFI_RESTORE r11
	addq $0x30,%rsp
	CFI_ADJUST_CFA_OFFSET -0x30
	pushq_cfi $-1 /* orig_ax = -1 => not a system call */
	SAVE_ALL
	jmp error_exit
	CFI_ENDPROC
END(xen_failsafe_callback)

apicinterrupt HYPERVISOR_CALLBACK_VECTOR \
	xen_hvm_callback_vector xen_evtchn_do_upcall

#endif /* CONFIG_PARAVIRT_XEN */

#if defined(CONFIG_HYPERV) || defined(CONFIG_HYPERV_MODULE)
apicinterrupt HYPERVISOR_CALLBACK_VECTOR \
	hyperv_callback_vector hyperv_vector_handler
#endif /* CONFIG_HYPERV */

#ifdef	CONFIG_KDB

#ifdef CONFIG_SMP
apicinterrupt LKDB_VECTOR \
	kdb_interrupt, smp_kdb_interrupt
#endif	/* CONFIG_SMP */

ENTRY(kdb_call)
	INTR_FRAME
	cld
	pushq $-1			# orig_eax
	CFI_ADJUST_CFA_OFFSET 8
	SAVE_ALL
	movq $1,%rdi			# LKDB_REASON_ENTER
	movq $0,%rsi			# error_code
	movq %rsp,%rdx			# struct pt_regs
	call kdb
	RESTORE_ALL
	addq $8,%rsp			# forget orig_eax
	CFI_ADJUST_CFA_OFFSET -8
	iretq
	CFI_ENDPROC
END(kdb_call)

#endif	/* CONFIG_KDB */


/*
 * Some functions should be protected against kprobes
 */
	.pushsection .kprobes.text, "ax"

paranoidzeroentry_ist debug do_debug DEBUG_STACK
zeroentry int3 do_int3
errorentry stack_segment do_stack_segment
#ifdef CONFIG_PARAVIRT_XEN
zeroentry xen_debug do_debug
zeroentry xen_int3 do_int3
errorentry xen_stack_segment do_stack_segment
#endif
errorentry general_protection do_general_protection
errorentry page_fault do_page_fault
#ifdef CONFIG_KVM_GUEST
errorentry async_page_fault do_async_page_fault
#endif
#ifdef CONFIG_X86_MCE
paranoidzeroentry machine_check *machine_check_vector(%rip)
#endif

	/*
	 * "Paranoid" exit path from exception stack.
	 * Paranoid because this is used by NMIs and cannot take
	 * any kernel state for granted.
	 * We don't do kernel preemption checks here, because only
	 * NMI should be common and it does not enable IRQs and
	 * cannot get reschedule ticks.
	 *
	 * "trace" is 0 for the NMI handler only, because irq-tracing
	 * is fundamentally NMI-unsafe. (we cannot change the soft and
	 * hard flags at once, atomically)
	 */
ENTRY(paranoid_exit)
	DEFAULT_FRAME
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	testl %ebx,%ebx				/* swapgs needed? */
	jnz paranoid_noswapgs_restore
	testl $3,CS(%rsp)
	jnz   paranoid_userspace
paranoid_swapgs_restore:
	/* return to kernel with swapgs */
	RESTORE_CR3 scratch_reg=%rax save_reg=%r14
	RESTORE_ALL 8
	SWAPGS
	jmp irq_return
paranoid_noswapgs_restore:
	/* return to kernel */
	RESTORE_CR3 scratch_reg=%rax save_reg=%r14
	RESTORE_ALL 8
	jmp irq_return
paranoid_userspace:
	GET_THREAD_INFO(%rcx)
	movl TI_flags(%rcx),%ebx
	andl $_TIF_WORK_MASK,%ebx
	jz paranoid_userspace_restore_all
	movq %rsp,%rdi			/* &pt_regs */
	call sync_regs
	movq %rax,%rsp			/* switch stack for scheduling */
	testl $_TIF_NEED_RESCHED,%ebx
	jnz paranoid_schedule
	movl %ebx,%edx			/* arg3: thread flags */
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_NONE)
	xorl %esi,%esi 			/* arg2: oldset */
	movq %rsp,%rdi 			/* arg1: &pt_regs */
	call do_notify_resume
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	jmp paranoid_userspace
paranoid_schedule:
	TRACE_IRQS_ON
	ENABLE_INTERRUPTS(CLBR_ANY)
	call schedule
	DISABLE_INTERRUPTS(CLBR_ANY)
	TRACE_IRQS_OFF
	jmp paranoid_userspace
	CFI_ENDPROC
END(paranoid_exit)

/*
 * Exception entry point. This expects an error code/orig_rax on the stack.
 * returns in "no swapgs flag" in %ebx.
 */
ENTRY(error_entry)
	XCPT_FRAME offset=ORIG_RAX-R15+8
	/* oldrax contains error code */
	cld
	movq %rdi, RDI+8(%rsp)
	movq %rsi, RSI+8(%rsp)
	movq %rdx, RDX+8(%rsp)
	movq %rcx, RCX+8(%rsp)
	movq %rax, RAX+8(%rsp)
	movq  %r8,  R8+8(%rsp)
	movq  %r9,  R9+8(%rsp)
	movq %r10, R10+8(%rsp)
	movq %r11, R11+8(%rsp)
	movq_cfi rbx, RBX+8
	movq %rbp, RBP+8(%rsp)
	movq %r12, R12+8(%rsp)
	movq %r13, R13+8(%rsp)
	movq %r14, R14+8(%rsp)
	movq %r15, R15+8(%rsp)
	/*
	 * error_entry() always returns with a kernel gsbase and
	 * CR3.  We must also have a kernel CR3/gsbase before
	 * calling TRACE_IRQS_*.  Just unconditionally switch to
	 * the kernel CR3 here.
	 */
	xorl %ebx,%ebx
	testl $3,CS+8(%rsp)
	je error_kernelspace
error_swapgs:
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	SWITCH_KERNEL_CR3
error_sti:
	movq PER_CPU_VAR(init_tss + TSS_sp0), %rcx
	cmpq %rcx, %rsp
	ja 1f
	leaq -TSS_stack_size(%rcx), %rax
	cmpq %rsp, %rax
	ja 1f
	subq %rsp, %rcx
	movq PER_CPU_VAR(kernel_stack), %rdi
	addq $KERNEL_STACK_OFFSET, %rdi
	subq %rcx, %rdi
	movq %rdi, %rax
	movq %rsp, %rsi
	rep movsb
	movq %rax, %rsp

	ENABLE_IBRS
1:
	TRACE_IRQS_OFF
	ret

/*
 * There are two places in the kernel that can potentially fault with
 * usergs. Handle them here.  B stepping K8s sometimes report a
 * truncated RIP for IRET exceptions returning to compat mode. Check
 * for these here too.
 */
error_kernelspace:
	CFI_REL_OFFSET rcx, RCX+8
	incl %ebx
	leaq native_irq_return_iret(%rip),%rcx
	cmpq %rcx,RIP+8(%rsp)
	je error_bad_iret
	movl %ecx,%eax	/* zero extend */
	cmpq %rax,RIP+8(%rsp)
	je bstep_iret
	cmpq $gs_change,RIP+8(%rsp)
	je error_swapgs
	FENCE_SWAPGS_KERNEL_ENTRY
	jmp error_sti

bstep_iret:
	/* Fix truncated RIP */
	movq %rcx,RIP+8(%rsp)
	/* fall through */

error_bad_iret:
	SWAPGS
	FENCE_SWAPGS_USER_ENTRY
	SWITCH_KERNEL_CR3
	mov %rsp,%rdi
	call fixup_bad_iret
	mov %rax,%rsp
	decl %ebx	/* Return to usergs */
	jmp error_sti
	CFI_ENDPROC
END(error_entry)


/* ebx:	no swapgs flag (1: don't need swapgs, 0: need it) */
ENTRY(error_exit)
	DEFAULT_FRAME
	movl %ebx,%eax
	RESTORE_REST
	DISABLE_INTERRUPTS(CLBR_NONE)
	TRACE_IRQS_OFF
	GET_THREAD_INFO(%rcx)
	testl %eax,%eax
	jne retint_kernel
	LOCKDEP_SYS_EXIT_IRQ
	movl TI_flags(%rcx),%edx
	movl $_TIF_WORK_MASK,%edi
	andl %edi,%edx
	jnz retint_careful
	jmp retint_swapgs
	CFI_ENDPROC
END(error_exit)

/*
 * Test if a given stack is an NMI stack or not.
 */
	.macro test_in_nmi reg stack nmi_ret normal_ret
	cmpq %\reg, \stack
	ja \normal_ret
	subq $EXCEPTION_STKSZ, %\reg
	cmpq %\reg, \stack
	jb \normal_ret
	jmp \nmi_ret
	.endm

	/* runs on exception stack */
ENTRY(nmi)
	INTR_FRAME

	/*
	 * Fix up the exception frame if we're on Xen.
	 * PARAVIRT_ADJUST_EXCEPTION_FRAME is guaranteed to push at most
	 * one value to the stack on native, so it may clobber the rdx
	 * scratch slot, but it won't clobber any of the important
	 * slots past it.
	 *
	 * Xen is a different story, because the Xen frame itself overlaps
	 * the "NMI executing" variable.
	 */
	PARAVIRT_ADJUST_EXCEPTION_FRAME

	/*
	 * We allow breakpoints in NMIs. If a breakpoint occurs, then
	 * the iretq it performs will take us out of NMI context.
	 * This means that we can have nested NMIs where the next
	 * NMI is using the top of the stack of the previous NMI. We
	 * can't let it execute because the nested NMI will corrupt the
	 * stack of the previous NMI. NMI handlers are not re-entrant
	 * anyway.
	 *
	 * To handle this case we do the following:
	 *  Check the a special location on the stack that contains
	 *  a variable that is set when NMIs are executing.
	 *  The interrupted task's stack is also checked to see if it
	 *  is an NMI stack.
	 *  If the variable is not set and the stack is not the NMI
	 *  stack then:
	 *    o Set the special variable on the stack
	 *    o Copy the interrupt frame into a "saved" location on the stack
	 *    o Copy the interrupt frame into a "copy" location on the stack
	 *    o Continue processing the NMI
	 *  If the variable is set or the previous stack is the NMI stack:
	 *    o Modify the "copy" location to jump to the repeate_nmi
	 *    o return back to the first NMI
	 *
	 * Now on exit of the first NMI, we first clear the stack variable
	 * The NMI stack will tell any nested NMIs at that point that it is
	 * nested. Then we pop the stack normally with iret, and if there was
	 * a nested NMI that updated the copy interrupt stack frame, a
	 * jump will be made to the repeat_nmi code that will handle the second
	 * NMI.
	 *
	 * However, espfix prevents us from directly returning to userspace
	 * with a single IRET instruction.  Similarly, IRET to user mode
	 * can fault.  We therefore handle NMIs from user space like
	 * other IST entries.
	 */

	/* Use %rdx as out temp variable throughout */
	pushq_cfi %rdx

	testb	$3, CS-RIP+8(%rsp)
	jz	.Lnmi_from_kernel

	/*
	 * NMI from user mode.  We need to run on the thread stack, but we
	 * can't go through the normal entry paths: NMIs are masked, and
	 * we don't want to enable interrupts, because then we'll end
	 * up in an awkward situation in which IRQs are on but NMIs
	 * are off.
	 *
	 * We also must not push anything to the stack before switching
	 * stacks lest we corrupt the "NMI executing" variable.
	 */

	SWAPGS_UNSAFE_STACK
	SWITCH_KERNEL_CR3_NO_STACK

	cld
	movq	%rsp, %rdx
	movq	PER_CPU_VAR(kernel_stack), %rsp
	addq	$KERNEL_STACK_OFFSET, %rsp
	pushq	5*8(%rdx)	/* pt_regs->ss */
	pushq	4*8(%rdx)	/* pt_regs->rsp */
	pushq	3*8(%rdx)	/* pt_regs->flags */
	pushq	2*8(%rdx)	/* pt_regs->cs */
	pushq	1*8(%rdx)	/* pt_regs->rip */
	pushq   $-1		/* pt_regs->orig_ax */
	pushq   %rdi		/* pt_regs->di */
	pushq   %rsi		/* pt_regs->si */
	pushq   (%rdx)		/* pt_regs->dx */
	pushq   %rcx		/* pt_regs->cx */
	pushq   %rax		/* pt_regs->ax */
	pushq   %r8		/* pt_regs->r8 */
	pushq   %r9		/* pt_regs->r9 */
	pushq   %r10		/* pt_regs->r10 */
	pushq   %r11		/* pt_regs->r11 */
	pushq	%rbx		/* pt_regs->rbx */
	pushq	%rbp		/* pt_regs->rbp */
	pushq	%r12		/* pt_regs->r12 */
	pushq	%r13		/* pt_regs->r13 */
	pushq	%r14		/* pt_regs->r14 */
	pushq	%r15		/* pt_regs->r15 */

	ENABLE_IBRS

	/*
	 * At this point we no longer need to worry about stack damage
	 * due to nesting -- we're on the normal thread stack and we're
	 * done with the NMI stack.
	 */

	movq	%rsp, %rdi
	movq	$-1, %rsi
	call	do_nmi

	DISABLE_IBRS

	/*
	 * Return back to user mode.  We must *not* do the normal exit
	 * work, because we don't want to enable interrupts.  Fortunately,
	 * do_nmi doesn't modify pt_regs.
	 */
        addq    $6*8, %rsp      /* skip bx, bp, and r12-r15 */
        jmp     swapgs_restore_regs_and_return_to_usermode

.Lnmi_from_kernel:
	/*
	 * Check the special variable on the stack to see if NMIs are
	 * executing.
	 */
	cmp $1, -8(%rsp)
	je nested_nmi

	/*
	 * Now test if the previous stack was an NMI stack.
	 * We need the double check. We check the NMI stack to satisfy the
	 * race when the first NMI clears the variable before returning.
	 * We check the variable because the first NMI could be in a
	 * breakpoint routine using a breakpoint stack.
	 */
	lea 6*8(%rsp), %rdx
	test_in_nmi rdx, 4*8(%rsp), nested_nmi, first_nmi

nested_nmi:
	/*
	 * Do nothing if we interrupted the fixup in repeat_nmi.
	 * It's about to repeat the NMI handler, so we are fine
	 * with ignoring this one.
	 */
	movq $repeat_nmi, %rdx
	cmpq 8(%rsp), %rdx
	ja 1f
	movq $end_repeat_nmi, %rdx
	cmpq 8(%rsp), %rdx
	ja nested_nmi_out

1:
	/* Set up the interrupted NMIs stack to jump to repeat_nmi */
	leaq -6*8(%rsp), %rdx
	movq %rdx, %rsp
	CFI_ADJUST_CFA_OFFSET 6*8
	pushq_cfi $__KERNEL_DS
	pushq_cfi %rdx
	pushfq_cfi
	pushq_cfi $__KERNEL_CS
	pushq_cfi $repeat_nmi

	/* Put stack back */
	addq $(11*8), %rsp
	CFI_ADJUST_CFA_OFFSET -11*8

nested_nmi_out:
	popq_cfi %rdx

	/* No need to check faults here */
	INTERRUPT_RETURN

first_nmi:
	/*
	 * Because nested NMIs will use the pushed location that we
	 * stored in rdx, we must keep that space available.
	 * Here's what our stack frame will look like:
	 * +-------------------------+
	 * | original SS             |
	 * | original Return RSP     |
	 * | original RFLAGS         |
	 * | original CS             |
	 * | original RIP            |
	 * +-------------------------+
	 * | temp storage for rdx    |
	 * +-------------------------+
	 * | NMI executing variable  |
	 * +-------------------------+
	 * | Saved SS                |
	 * | Saved Return RSP        |
	 * | Saved RFLAGS            |
	 * | Saved CS                |
	 * | Saved RIP               |
	 * +-------------------------+
	 * | copied SS               |
	 * | copied Return RSP       |
	 * | copied RFLAGS           |
	 * | copied CS               |
	 * | copied RIP              |
	 * +-------------------------+
	 * | pt_regs                 |
	 * +-------------------------+
	 *
	 * The saved RIP is used to fix up the copied RIP that a nested
	 * NMI may zero out. The original stack frame and the temp storage
	 * is also used by nested NMIs and can not be trusted on exit.
	 */
	/* Set the NMI executing variable on the stack. */
	pushq_cfi $1

	/* Copy the stack frame to the Saved frame */
	.rept 5
	pushq_cfi 6*8(%rsp)
	.endr

	/* Make another copy, this one may be modified by nested NMIs */
	.rept 5
	pushq_cfi 4*8(%rsp)
	.endr

	/* Do not pop rdx, nested NMIs will corrupt it */
	movq 11*8(%rsp), %rdx

	/*
	 * Everything below this point can be preempted by a nested
	 * NMI if the first NMI took an exception. Repeated NMIs
	 * caused by an exception and nested NMI will start here, and
	 * can still be preempted by another NMI.
	 */
restart_nmi:
	pushq_cfi $-1
	subq $ORIG_RAX-R15, %rsp
	CFI_ADJUST_CFA_OFFSET ORIG_RAX-R15
	call save_paranoid
	DEFAULT_FRAME -1

	/*
	 * Save off the CR2 register. If we take a page fault in the NMI then
	 * it could corrupt the CR2 value. If the NMI preempts a page fault
	 * handler before it was able to read the CR2 register, and then the
	 * NMI itself takes a page fault, the page fault that was preempted
	 * will read the information from the NMI page fault and not the
	 * origin fault. Save it off and restore it if it changes.
	 * Use the r12 callee-saved register.
	 */
	movq %cr2, %r12

	/* paranoidentry do_nmi, 0; without TRACE_IRQS_OFF */
	movq %rsp,%rdi
	movq $-1,%rsi
	call do_nmi

	DISABLE_IBRS
	RESTORE_CR3 scratch_reg=%rax save_reg=%r14

	/* Did the NMI take a page fault? Restore cr2 if it did */
	movq %cr2, %rcx
	cmpq %rcx, %r12
	je 1f
	movq %r12, %cr2
1:
	testl %ebx,%ebx                         /* swapgs needed? */
	jnz nmi_restore
nmi_swapgs:
	SWAPGS_UNSAFE_STACK
nmi_restore:
	/* Skip the additional iret frame */
	RESTORE_ALL 6*8

	movq    $0, 5*8(%rsp)           /* clear "NMI executing" */

	/*
	 * Skip CLEAR_CPU_BUFFERS here, since it only helps in rare cases like
	 * NMI in kernel after user state is restored. For an unprivileged user
	 * these conditions are hard to meet.
	 /*

	/*
	 * INTERRUPT_RETURN reads the "iret" frame and exits the NMI
	 * stack in a single instruction.  We are returning to kernel
	 * mode, so this cannot result in a fault.
	 */
	INTERRUPT_RETURN
	CFI_ENDPROC

END(nmi)

	/*
	 * If an NMI hit an iret because of an exception or breakpoint,
	 * it can lose its NMI context, and a nested NMI may come in.
	 * In that case, the nested NMI will change the preempted NMI's
	 * stack to jump to here when it does the final iret.
	 */
repeat_nmi:
	INTR_FRAME
	/* Update the stack variable to say we are still in NMI */
	movq $1, 5*8(%rsp)

	/* copy the saved stack back to copy stack */
	.rept 5
	pushq_cfi 4*8(%rsp)
	.endr

	jmp restart_nmi
	CFI_ENDPROC
end_repeat_nmi:

ENTRY(ignore_sysret)
	CFI_STARTPROC
	mov $-ENOSYS,%eax
	CLEAR_CPU_BUFFERS
	sysret
	CFI_ENDPROC
END(ignore_sysret)

/*
 * End of kprobes section
 */
	.popsection
