/* SPDX-License-Identifier: GPL-2.0 */
/*
 * String handling functions.
 *
 * Copyright IBM Corp. 2012
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/nospec-insn.h>

	GEN_BR_THUNK %r14

/*
 * __memset16/32/64
 *
 * void *__memset16(uint16_t *s, uint16_t v, size_t count)
 * void *__memset32(uint32_t *s, uint32_t v, size_t count)
 * void *__memset64(uint64_t *s, uint64_t v, size_t count)
 */
.macro __MEMSET bits,bytes,insn
SYM_FUNC_START(__memset\bits)
	ltgr	%r4,%r4
	jz	.L__memset_exit\bits
	cghi	%r4,\bytes
	je	.L__memset_store\bits
	aghi	%r4,-(\bytes+1)
	srlg	%r5,%r4,8
	ltgr	%r5,%r5
	lgr	%r1,%r2
	jz	.L__memset_remainder\bits
.L__memset_loop\bits:
	\insn	%r3,0(%r1)
	mvc	\bytes(256-\bytes,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r5,.L__memset_loop\bits
.L__memset_remainder\bits:
	\insn	%r3,0(%r1)
	exrl	%r4,.L__memset_mvc\bits
	BR_EX	%r14
.L__memset_store\bits:
	\insn	%r3,0(%r2)
.L__memset_exit\bits:
	BR_EX	%r14
.L__memset_mvc\bits:
	mvc	\bytes(1,%r1),0(%r1)
SYM_FUNC_END(__memset\bits)
.endm

__MEMSET 16,2,sth
EXPORT_SYMBOL(__memset16)

__MEMSET 32,4,st
EXPORT_SYMBOL(__memset32)

__MEMSET 64,8,stg
EXPORT_SYMBOL(__memset64)
