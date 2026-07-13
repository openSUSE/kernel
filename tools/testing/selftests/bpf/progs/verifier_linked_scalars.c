// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("scalars: find linked scalars")
__failure
__msg("math between fp pointer and 2147483647 is not allowed")
__naked void scalars(void)
{
	asm volatile ("				\
	r0 = 0;					\
	r1 = 0x80000001 ll;			\
	r1 /= 1;				\
	r2 = r1;				\
	r4 = r1;				\
	w2 += 0x7FFFFFFF;			\
	w4 += 0;				\
	if r2 == 0 goto l1;			\
	exit;					\
l1:						\
	r4 >>= 63;				\
	r3 = 1;					\
	r3 -= r4;				\
	r3 *= 0x7FFFFFFF;			\
	r3 += r10;				\
	*(u8*)(r3 - 1) = r0;			\
	exit;					\
"	::: __clobber_all);
}

/*
 * Test that sync_linked_regs() preserves register IDs.
 *
 * The sync_linked_regs() function copies bounds from known_reg to linked
 * registers. When doing so, it must preserve each register's original id
 * to allow subsequent syncs from the same source to work correctly.
 *
 */
SEC("socket")
__success
__naked void sync_linked_regs_preserves_id(void)
{
	asm volatile ("						\
	call %[bpf_get_prandom_u32];				\
	r0 &= 0xff;	/* r0 in [0, 255] */			\
	r1 = r0;	/* r0, r1 linked with id 1 */		\
	r1 += 4;	/* r1 has id=1 and off=4 in [4, 259] */ \
	if r1 < 10 goto l0_%=;					\
	/* r1 in [10, 259], r0 synced to [6, 255] */		\
	r2 = r0;	/* r2 has id=1 and in [6, 255] */	\
	if r1 < 14 goto l0_%=;					\
	/* r1 in [14, 259], r0 synced to [10, 255] */		\
	if r0 >= 10 goto l0_%=;					\
	/* Never executed */					\
	r0 /= 0;						\
l0_%=:								\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Test that regsafe() verifies base_id consistency for BPF_ADD_CONST
 * linked scalars during state pruning.
 *
 * The false branch (explored first) links R3 to R2 via ADD_CONST.
 * The true branch (runtime path) links R3 to R4 (unrelated base_id).
 * At the merge point, pruning must fail because the linkage topology
 * differs.
 */
SEC("socket")
__description("linked scalars: add_const base_id must be consistent for pruning")
__failure __msg("invalid variable-offset")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void add_const_base_id_pruning(void)
{
	asm volatile ("						\
	r1 = 0;							\
	*(u64*)(r10 - 16) = r1;					\
	call %[bpf_get_prandom_u32];				\
	r6 = r0;						\
	r6 &= 1;						\
	if r6 >= 1 goto l_true_%=;				\
								\
	/* False branch (explored first, old state) */		\
	call %[bpf_get_prandom_u32];				\
	r2 = r0;						\
	r2 &= 0xff;		/* R2 = scalar(id=A) [0,255] */	\
	r3 = r2;		/* R3 linked to R2 (id=A) */	\
	r3 += 10;		/* R3 id=A|ADD_CONST, delta=10 */\
	r6 = 0;							\
	goto l_merge_%=;					\
								\
l_true_%=:							\
	/* True branch (runtime path, cur state) */		\
	call %[bpf_get_prandom_u32];				\
	r2 = r0;						\
	r2 &= 0xff;		/* R2 = scalar [0,255], id=0 */	\
	r4 = r0;						\
	r4 &= 0xff;		/* R4 = scalar [0,255], id=0 */	\
	r3 = r4;		/* R3 linked to R4 (new id=C) */\
	r3 += 10;		/* R3 id=C|ADD_CONST, delta=10 */\
	r6 = 0;							\
								\
l_merge_%=:							\
	/* At merge, old R3 linked to R2, cur R3 linked to R4. */\
	/* Pruning must fail: base_ids A vs C inconsistent. */	\
	if r2 >= 6 goto l_exit_%=;				\
	/* sync_linked_regs: R2<6 => R3<16 in old state. */	\
	/* Without fix: R3 in [10,15] from incorrect pruning. */\
	/* With fix: R3 in [10,265], not synced from R2. */	\
	r3 -= 10;		/* [0,5] vs [0,255] */		\
	r9 = r10;						\
	r9 += -16;						\
	r9 += r3;		/* fp-16+[0,5] vs fp-16+[0,255] */\
	*(u8*)(r9 + 0) = r6;	/* within 16B vs past fp */	\
l_exit_%=:							\
	r0 = 0;							\
	exit;							\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
