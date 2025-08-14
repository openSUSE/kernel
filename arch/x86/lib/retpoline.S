/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/nops.h>

.macro THUNK reg
ENTRY(__x86_indirect_thunk_\reg)
	CFI_STARTPROC
	JMP_NOSPEC %\reg
	CFI_ENDPROC
.align 32, 0xcc		/* fill to the end of the line */
.skip  32, 0xcc		/* skip to the next upper half */
ENDPROC(__x86_indirect_thunk_\reg)
.endm

/*
 * Despite being an assembler file we can't just use .irp here
 * because __KSYM_DEPS__ only uses the C preprocessor and would
 * only see one instance of "__x86_indirect_thunk_\reg" rather
 * than one per register with the correct names. So we do it
 * the simple and nasty way...
 *
 * Worse, you can only have a single EXPORT_SYMBOL per line,
 * and CPP can't insert newlines, so we have to repeat everything
 * at least twice.
 */

#define __EXPORT_THUNK(sym)	_ASM_NOKPROBE(sym); EXPORT_SYMBOL(sym)
#define EXPORT_THUNK(reg)	__EXPORT_THUNK(__x86_indirect_thunk_ ## reg)

/* Instead of introducing a new set of thunks just ensure the existing RETPOLINE
 * thunks are properly aligned
 */
	.section .text..__x86.indirect_thunk
	.align 64, 0xcc
	.skip 32, 0xcc
SYM_CODE_START(__x86_indirect_its_thunk_array)

#undef GEN
#define GEN(reg) THUNK reg
#include <asm/GEN-for-each-reg.h>

	.align 64, 0xcc
SYM_CODE_END(__x86_indirect_its_thunk_array)

#undef GEN
#define GEN(reg) EXPORT_THUNK(reg)
#include <asm/GEN-for-each-reg.h>


/*
 * srso_alias_untrain_ret() and srso_alias_safe_ret() are placed at
 * special addresses:
 *
 * - srso_alias_untrain_ret() is 2M aligned
 * - srso_alias_safe_ret() is also in the same 2M page but bits 2, 8, 14
 * and 20 in its virtual address are set (while those bits in the
 * srso_alias_untrain_ret() function are cleared).
 *
 * This guarantees that those two addresses will alias in the branch
 * target buffer of Zen3/4 generations, leading to any potential
 * poisoned entries at that BTB slot to get evicted.
 *
 * As a result, srso_alias_safe_ret() becomes a safe return.
 */
#ifdef CONFIG_CPU_SRSO
	.section .text..__x86.rethunk_untrain

.globl srso_alias_untrain_ret;
srso_alias_untrain_ret:
	ASM_NOP2
	lfence
	jmp __x86_return_thunk
ENDPROC(srso_alias_untrain_ret)
__EXPORT_THUNK(srso_alias_untrain_ret)

	.section .text..__x86.rethunk_safe
#endif

/* Needs a definition for the __x86_return_thunk alternative below. */
.globl srso_alias_safe_ret;
srso_alias_safe_ret:
#ifdef CONFIG_CPU_SRSO
	lea 8(%_ASM_SP), %_ASM_SP
	UNWIND_HINT_FUNC
#endif
	ret
	int3
ENDPROC(srso_alias_safe_ret)


/*
 * This function name is magical and is used by -mfunction-return=thunk-extern
 * for the compiler to generate JMPs to it.
 */
	.section .text..__x86.return_thunk

/*
 * Safety details here pertain to the AMD Zen{1,2} microarchitecture:
 * 1) The RET at __x86_return_thunk must be on a 64 byte boundary, for
 *    alignment within the BTB.
 * 2) The instruction at retbleed_untrain_ret must contain, and not
 *    end with, the 0xc3 byte of the RET.
 * 3) STIBP must be enabled, or SMT disabled, to prevent the sibling thread
 *    from re-poisioning the BTB prediction.
 */
	.align 64
	.skip 64 - (__ret - retbleed_untrain_ret), 0xcc
.globl retbleed_untrain_ret;
retbleed_untrain_ret:

	/*
	 * As executed from retbleed_untrain_ret, this is:
	 *
	 *   TEST $0xcc, %bl
	 *   LFENCE
	 *   JMP __x86_return_thunk
	 *
	 * Executing the TEST instruction has a side effect of evicting any BTB
	 * prediction (potentially attacker controlled) attached to the RET, as
	 * __x86_return_thunk + 1 isn't an instruction boundary at the moment.
	 */
	.byte	0xf6

	/*
	 * As executed from __x86_return_thunk, this is a plain RET.
	 *
	 * As part of the TEST above, RET is the ModRM byte, and INT3 the imm8.
	 *
	 * We subsequently jump backwards and architecturally execute the RET.
	 * This creates a correct BTB prediction (type=ret), but in the
	 * meantime we suffer Straight Line Speculation (because the type was
	 * no branch) which is halted by the INT3.
	 *
	 * With SMT enabled and STIBP active, a sibling thread cannot poison
	 * RET's prediction to a type of its choice, but can evict the
	 * prediction due to competitive sharing. If the prediction is
	 * evicted, __x86_return_thunk will suffer Straight Line Speculation
	 * which will be contained safely by the INT3.
	 */

.globl __ret;
__ret:
	ret
	int3
ENDPROC(__ret)

	/*
	 * Ensure the TEST decoding / BTB invalidation is complete.
	 */
	lfence

	/*
	 * Jump back and execute the RET in the middle of the TEST instruction.
	 * INT3 is for SLS protection.
	 */
	jmp __ret
	int3
ENDPROC(retbleed_untrain_ret)
__EXPORT_THUNK(retbleed_untrain_ret)

/*
 * Some generic notes on the untraining sequences:
 *
 * They are interchangeable when it comes to flushing potentially wrong
 * RET predictions from the BTB.
 *
 * The SRSO Zen1/2 (MOVABS) untraining sequence is longer than the
 * Retbleed sequence because the return sequence done there
 * (srso_safe_ret()) is longer and the return sequence must fully nest
 * (end before) the untraining sequence. Therefore, the untraining
 * sequence must fully overlap the return sequence.
 *
 * Regarding alignment - the instructions which need to be untrained,
 * must all start at a cacheline boundary for Zen1/2 generations. That
 * is, instruction sequences starting at srso_safe_ret() and
 * the respective instruction sequences at retbleed_return_thunk()
 * must start at a cacheline boundary.
 */


/*
 * SRSO untraining sequence for Zen1/2, similar to retbleed_untrain_ret()
 * above. On kernel entry, srso_untrain_ret() is executed which is a
 *
 * movabs $0xccccc30824648d48,%rax
 *
 * and when the return thunk executes the inner label srso_safe_ret()
 * later, it is a stack manipulation and a RET which is mispredicted and
 * thus a "safe" one to use.
 */
	.align 64
	.skip 64 - (srso_safe_ret - srso_untrain_ret), 0xcc
.globl srso_untrain_ret;
srso_untrain_ret:
	.byte 0x48, 0xb8

.globl srso_safe_ret;
srso_safe_ret:
	lea 8(%_ASM_SP), %_ASM_SP
	ret
	int3
	int3
	lfence
	call srso_safe_ret
	ud2
ENDPROC(srso_safe_ret)
ENDPROC(srso_untrain_ret)
__EXPORT_THUNK(srso_untrain_ret)


ENTRY(entry_untrain_ret)
	ALTERNATIVE "jmp retbleed_untrain_ret", "jmp srso_untrain_ret", X86_FEATURE_SRSO
ENDPROC(entry_untrain_ret)
__EXPORT_THUNK(entry_untrain_ret)

.align 64, 0xcc
.skip 32, 0xcc
ENTRY(__x86_return_thunk)
	UNWIND_HINT_FUNC
	ALTERNATIVE_2 "jmp __ret", "call srso_safe_ret", X86_FEATURE_SRSO, \
			"call srso_alias_safe_ret", X86_FEATURE_SRSO_ALIAS
	ud2
ENDPROC(__x86_return_thunk)
EXPORT_SYMBOL(__x86_return_thunk)

