/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_STATIC_CALL_H
#define _ASM_STATIC_CALL_H

/*
 * The sequence below is laid out in a way that guarantees that the literal and
 * the instruction are always covered by the same cacheline, and can be updated
 * using a single store-pair instruction (provided that we rewrite the BTI C
 * instruction as well). This means the literal and the instruction are always
 * in sync when observed via the D-side.
 *
 * However, this does not guarantee that the I-side will catch up immediately
 * as well: until the I-cache maintenance completes, CPUs may branch to the old
 * target, or execute a stale NOP or RET. We deal with this by writing the
 * literal unconditionally, even if it is 0x0 or the branch is in range. That
 * way, a stale NOP will fall through and call the new target via an indirect
 * call. Stale RETs or Bs will be taken as before, and branch to the old
 * target.
 */
#define __ARCH_DEFINE_STATIC_CALL_TRAMP(name, insn)			    \
	asm("	.pushsection	.static_call.text, \"ax\"		\n" \
	    "	.align		4					\n" \
	    "	.globl		" STATIC_CALL_TRAMP_STR(name) "		\n" \
	    "0:	.quad		0x0					\n" \
	    STATIC_CALL_TRAMP_STR(name) ":				\n" \
	    "	hint 		34	/* BTI C */			\n" \
		insn "							\n" \
	    "	ldr		x16, 0b					\n" \
	    "	cbz		x16, 1f					\n" \
	    "	br		x16					\n" \
	    "1:	ret							\n" \
	    "	.popsection						\n")

#define ARCH_DEFINE_STATIC_CALL_TRAMP(name, func)			\
	__ARCH_DEFINE_STATIC_CALL_TRAMP(name, "b " #func)

#define ARCH_DEFINE_STATIC_CALL_NULL_TRAMP(name)			\
	__ARCH_DEFINE_STATIC_CALL_TRAMP(name, "ret")

#endif /* _ASM_STATIC_CALL_H */
