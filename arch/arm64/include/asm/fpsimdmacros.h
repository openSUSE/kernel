/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FP/SIMD state saving and restoring macros
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <asm/assembler.h>

.macro fpsimd_save state, tmpnr
	stp	q0, q1, [\state, #16 * 0]
	stp	q2, q3, [\state, #16 * 2]
	stp	q4, q5, [\state, #16 * 4]
	stp	q6, q7, [\state, #16 * 6]
	stp	q8, q9, [\state, #16 * 8]
	stp	q10, q11, [\state, #16 * 10]
	stp	q12, q13, [\state, #16 * 12]
	stp	q14, q15, [\state, #16 * 14]
	stp	q16, q17, [\state, #16 * 16]
	stp	q18, q19, [\state, #16 * 18]
	stp	q20, q21, [\state, #16 * 20]
	stp	q22, q23, [\state, #16 * 22]
	stp	q24, q25, [\state, #16 * 24]
	stp	q26, q27, [\state, #16 * 26]
	stp	q28, q29, [\state, #16 * 28]
	stp	q30, q31, [\state, #16 * 30]!
	mrs	x\tmpnr, fpsr
	str	w\tmpnr, [\state, #16 * 2]
	mrs	x\tmpnr, fpcr
	str	w\tmpnr, [\state, #16 * 2 + 4]
.endm

.macro fpsimd_restore_fpcr state, tmp
	/*
	 * Writes to fpcr may be self-synchronising, so avoid restoring
	 * the register if it hasn't changed.
	 */
	mrs	\tmp, fpcr
	cmp	\tmp, \state
	b.eq	9999f
	msr	fpcr, \state
9999:
.endm

/* Clobbers \state */
.macro fpsimd_restore state, tmpnr
	ldp	q0, q1, [\state, #16 * 0]
	ldp	q2, q3, [\state, #16 * 2]
	ldp	q4, q5, [\state, #16 * 4]
	ldp	q6, q7, [\state, #16 * 6]
	ldp	q8, q9, [\state, #16 * 8]
	ldp	q10, q11, [\state, #16 * 10]
	ldp	q12, q13, [\state, #16 * 12]
	ldp	q14, q15, [\state, #16 * 14]
	ldp	q16, q17, [\state, #16 * 16]
	ldp	q18, q19, [\state, #16 * 18]
	ldp	q20, q21, [\state, #16 * 20]
	ldp	q22, q23, [\state, #16 * 22]
	ldp	q24, q25, [\state, #16 * 24]
	ldp	q26, q27, [\state, #16 * 26]
	ldp	q28, q29, [\state, #16 * 28]
	ldp	q30, q31, [\state, #16 * 30]!
	ldr	w\tmpnr, [\state, #16 * 2]
	msr	fpsr, x\tmpnr
	ldr	w\tmpnr, [\state, #16 * 2 + 4]
	fpsimd_restore_fpcr x\tmpnr, \state
.endm

/* Sanity-check macros to help avoid encoding garbage instructions */

.macro _check_general_reg nr
	.if (\nr) < 0 || (\nr) > 30
		.error "Bad register number \nr."
	.endif
.endm

.macro _sve_check_zreg znr
	.if (\znr) < 0 || (\znr) > 31
		.error "Bad Scalable Vector Extension vector register number \znr."
	.endif
.endm

.macro _sve_check_preg pnr
	.if (\pnr) < 0 || (\pnr) > 15
		.error "Bad Scalable Vector Extension predicate register number \pnr."
	.endif
.endm

.macro _check_num n, min, max
	.if (\n) < (\min) || (\n) > (\max)
		.error "Number \n out of range [\min,\max]"
	.endif
.endm

.macro _sme_check_wv v
	.if (\v) < 12 || (\v) > 15
		.error "Bad vector select register \v."
	.endif
.endm

/* Deprecated macros for SVE instructions */

/* STR (vector): STR Z\nz, [X\nxbase, #\offset, MUL VL] */
.macro _sve_str_v nz, nxbase, offset=0
	.arch_extension sve
	str	z\nz, [X\nxbase, #\offset, MUL VL]
.endm

/* LDR (vector): LDR Z\nz, [X\nxbase, #\offset, MUL VL] */
.macro _sve_ldr_v nz, nxbase, offset=0
	.arch_extension sve
	ldr	z\nz, [X\nxbase, #\offset, MUL VL]
.endm

/* STR (predicate): STR P\np, [X\nxbase, #\offset, MUL VL] */
.macro _sve_str_p np, nxbase, offset=0
	.arch_extension sve
	str	p\np, [X\nxbase, #\offset, MUL VL]
.endm

/* LDR (predicate): LDR P\np, [X\nxbase, #\offset, MUL VL] */
.macro _sve_ldr_p np, nxbase, offset=0
	.arch_extension sve
	ldr p\np, [x\nxbase, #\offset, MUL VL]
.endm

/* RDVL X\nx, #\imm */
.macro _sve_rdvl nx, imm
	.arch_extension sve
	rdvl x\nx, #\imm
.endm

/* RDFFR (unpredicated): RDFFR P\np.B */
.macro _sve_rdffr np
	.arch_extension sve
	rdffr p\np\().b
.endm

/* WRFFR P\np.B */
.macro _sve_wrffr np
	.arch_extension sve
	wrffr p\np\().b
.endm

/* PFALSE P\np.B */
.macro _sve_pfalse np
	.arch_extension sve
	pfalse	p\np\().b
.endm

/* Deprecated macros for SME instructions */

/* RDSVL X\nx, #\imm */
.macro _sme_rdsvl nx, imm
	.arch_extension sme
	rdsvl x\nx, #\imm
.endm

/*
 * STR (vector from ZA array):
 *	STR ZA[W\nw, #\offset], [X\nxbase, #\offset, MUL VL]
 */
.macro _sme_str_zav nw, nxbase, offset=0
	.arch_extension sme
	str	za[w\nw, #\offset], [x\nxbase, #\offset, MUL VL]
.endm

/*
 * LDR (vector to ZA array):
 *	LDR ZA[w\nw, #\offset], [X\nxbase, #\offset, MUL VL]
 */
.macro _sme_ldr_zav nw, nxbase, offset=0
	.arch_extension sme
	ldr	za[w\nw, #\offset], [x\nxbase, #\offset, MUL VL]
.endm

/*
 * SME2 instruction encodings for older assemblers.
 * Supported by binutils 2.41+.
 * Supported by LLVM 16+
 */

/*
 * LDR (ZT0)
 *
 *	LDR ZT0, nx
 */
.macro _ldr_zt nx
	_check_general_reg \nx
	.inst	0xe11f8000	\
		 | (\nx << 5)
.endm

/*
 * STR (ZT0)
 *
 *	STR ZT0, nx
 */
.macro _str_zt nx
	_check_general_reg \nx
	.inst	0xe13f8000		\
		| (\nx << 5)
.endm

.macro __for from:req, to:req
	.if (\from) == (\to)
		_for__body %\from
	.else
		__for %\from, %((\from) + ((\to) - (\from)) / 2)
		__for %((\from) + ((\to) - (\from)) / 2 + 1), %\to
	.endif
.endm

.macro _for var:req, from:req, to:req, insn:vararg
	.macro _for__body \var:req
		.noaltmacro
		\insn
		.altmacro
	.endm

	.altmacro
	__for \from, \to
	.noaltmacro

	.purgem _for__body
.endm

/* Preserve the first 128-bits of Znz and zero the rest. */
.macro _sve_flush_z nz
	_sve_check_zreg \nz
	mov	v\nz\().16b, v\nz\().16b
.endm

.macro sve_flush_z
 _for n, 0, 31, _sve_flush_z	\n
.endm
.macro sve_flush_p
 _for n, 0, 15, _sve_pfalse	\n
.endm
.macro sve_flush_ffr
		_sve_wrffr	0
.endm

.macro sve_save nxbase, xpfpsr, save_ffr, nxtmp
 _for n, 0, 31,	_sve_str_v	\n, \nxbase, \n - 34
 _for n, 0, 15,	_sve_str_p	\n, \nxbase, \n - 16
		cbz		\save_ffr, 921f
		_sve_rdffr	0
		b		922f
921:
		_sve_pfalse	0			// Zero out FFR
922:
		_sve_str_p	0, \nxbase
		_sve_ldr_p	0, \nxbase, -16
		mrs		x\nxtmp, fpsr
		str		w\nxtmp, [\xpfpsr]
		mrs		x\nxtmp, fpcr
		str		w\nxtmp, [\xpfpsr, #4]
.endm

.macro sve_load nxbase, xpfpsr, restore_ffr, nxtmp
 _for n, 0, 31,	_sve_ldr_v	\n, \nxbase, \n - 34
		cbz		\restore_ffr, 921f
		_sve_ldr_p	0, \nxbase
		_sve_wrffr	0
921:
 _for n, 0, 15,	_sve_ldr_p	\n, \nxbase, \n - 16

		ldr		w\nxtmp, [\xpfpsr]
		msr		fpsr, x\nxtmp
		ldr		w\nxtmp, [\xpfpsr, #4]
		msr		fpcr, x\nxtmp
.endm

.macro sme_save_za nxbase, xvl, nw
	mov	w\nw, #0

423:
	_sme_str_zav \nw, \nxbase
	add	x\nxbase, x\nxbase, \xvl
	add	x\nw, x\nw, #1
	cmp	\xvl, x\nw
	bne	423b
.endm

.macro sme_load_za nxbase, xvl, nw
	mov	w\nw, #0

423:
	_sme_ldr_zav \nw, \nxbase
	add	x\nxbase, x\nxbase, \xvl
	add	x\nw, x\nw, #1
	cmp	\xvl, x\nw
	bne	423b
.endm
