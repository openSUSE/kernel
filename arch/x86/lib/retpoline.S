/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/stringify.h>
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/cpufeatures.h>
#include <asm/alternative-asm.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>

.macro THUNK reg
	.section .text.__x86.indirect_thunk

ENTRY(__x86_indirect_thunk_\reg)
	CFI_STARTPROC
	JMP_NOSPEC %\reg
	CFI_ENDPROC
ENDPROC(__x86_indirect_thunk_\reg)
.endm

/*
 * Despite being an assembler file we can't just use .irp here
 * because __KSYM_DEPS__ only uses the C preprocessor and would
 * only see one instance of "__x86_indirect_thunk_\reg" rather
 * than one per register with the correct names. So we do it
 * the simple and nasty way...
 */
#define __EXPORT_THUNK(sym) _ASM_NOKPROBE(sym); EXPORT_SYMBOL(sym)
#define EXPORT_THUNK(reg) __EXPORT_THUNK(__x86_indirect_thunk_ ## reg)
#define GENERATE_THUNK(reg) THUNK reg ; EXPORT_THUNK(reg)

GENERATE_THUNK(_ASM_AX)
GENERATE_THUNK(_ASM_BX)
GENERATE_THUNK(_ASM_CX)
GENERATE_THUNK(_ASM_DX)
GENERATE_THUNK(_ASM_SI)
GENERATE_THUNK(_ASM_DI)
GENERATE_THUNK(_ASM_BP)
#ifdef CONFIG_64BIT
GENERATE_THUNK(r8)
GENERATE_THUNK(r9)
GENERATE_THUNK(r10)
GENERATE_THUNK(r11)
GENERATE_THUNK(r12)
GENERATE_THUNK(r13)
GENERATE_THUNK(r14)
GENERATE_THUNK(r15)
#endif

/*
 * This function name is magical and is used by -mfunction-return=thunk-extern
 * for the compiler to generate JMPs to it.
 */
	.section .text.__x86.return_thunk

/*
 * Safety details here pertain to the AMD Zen{1,2} microarchitecture:
 * 1) The RET at __x86_return_thunk must be on a 64 byte boundary, for
 *    alignment within the BTB.
 * 2) The instruction at zen_untrain_ret must contain, and not
 *    end with, the 0xc3 byte of the RET.
 * 3) STIBP must be enabled, or SMT disabled, to prevent the sibling thread
 *    from re-poisioning the BTB prediction.
 */
	.align 64
	.skip 63, 0xcc
SYM_FUNC_START_NOALIGN(zen_untrain_ret);

	/*
	 * As executed from zen_untrain_ret, this is:
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
SYM_INNER_LABEL(__x86_return_thunk, SYM_L_GLOBAL)
	ret
	int3
SYM_CODE_END(__x86_return_thunk)

	/*
	 * Ensure the TEST decoding / BTB invalidation is complete.
	 */
	lfence

	/*
	 * Jump back and execute the RET in the middle of the TEST instruction.
	 * INT3 is for SLS protection.
	 */
	jmp __x86_return_thunk
	int3
SYM_FUNC_END(zen_untrain_ret)
__EXPORT_THUNK(zen_untrain_ret)

EXPORT_SYMBOL(__x86_return_thunk)
