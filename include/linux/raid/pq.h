/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2003 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

#ifndef LINUX_RAID_RAID6_H
#define LINUX_RAID_RAID6_H

#include <linux/blkdev.h>
#include <linux/mm.h>

/* This should be const but the raid6 code is too convoluted for that. */
static inline void *raid6_get_zero_page(void)
{
	return page_address(ZERO_PAGE(0));
}

/* Routine choices */
struct raid6_calls {
	void (*gen_syndrome)(int, size_t, void **);
	void (*xor_syndrome)(int, int, int, size_t, void **);
	int  (*valid)(void);	/* Returns 1 if this routine set is usable */
	const char *name;	/* Name of this routine set */
	int priority;		/* Relative priority ranking if non-zero */
};

/* Selected algorithm */
extern struct raid6_calls raid6_call;

/* Various routine sets */
extern const struct raid6_calls raid6_intx1;
extern const struct raid6_calls raid6_intx2;
extern const struct raid6_calls raid6_intx4;
extern const struct raid6_calls raid6_intx8;
extern const struct raid6_calls raid6_mmxx1;
extern const struct raid6_calls raid6_mmxx2;
extern const struct raid6_calls raid6_sse1x1;
extern const struct raid6_calls raid6_sse1x2;
extern const struct raid6_calls raid6_sse2x1;
extern const struct raid6_calls raid6_sse2x2;
extern const struct raid6_calls raid6_sse2x4;
extern const struct raid6_calls raid6_altivec1;
extern const struct raid6_calls raid6_altivec2;
extern const struct raid6_calls raid6_altivec4;
extern const struct raid6_calls raid6_altivec8;
extern const struct raid6_calls raid6_avx2x1;
extern const struct raid6_calls raid6_avx2x2;
extern const struct raid6_calls raid6_avx2x4;
extern const struct raid6_calls raid6_avx512x1;
extern const struct raid6_calls raid6_avx512x2;
extern const struct raid6_calls raid6_avx512x4;
extern const struct raid6_calls raid6_s390vx8;
extern const struct raid6_calls raid6_vpermxor1;
extern const struct raid6_calls raid6_vpermxor2;
extern const struct raid6_calls raid6_vpermxor4;
extern const struct raid6_calls raid6_vpermxor8;
extern const struct raid6_calls raid6_lsx;
extern const struct raid6_calls raid6_lasx;
extern const struct raid6_calls raid6_rvvx1;
extern const struct raid6_calls raid6_rvvx2;
extern const struct raid6_calls raid6_rvvx4;
extern const struct raid6_calls raid6_rvvx8;

struct raid6_recov_calls {
	void (*data2)(int, size_t, int, int, void **);
	void (*datap)(int, size_t, int, void **);
	int  (*valid)(void);
	const char *name;
	int priority;
};

extern const struct raid6_recov_calls raid6_recov_intx1;
extern const struct raid6_recov_calls raid6_recov_ssse3;
extern const struct raid6_recov_calls raid6_recov_avx2;
extern const struct raid6_recov_calls raid6_recov_avx512;
extern const struct raid6_recov_calls raid6_recov_s390xc;
extern const struct raid6_recov_calls raid6_recov_neon;
extern const struct raid6_recov_calls raid6_recov_lsx;
extern const struct raid6_recov_calls raid6_recov_lasx;
extern const struct raid6_recov_calls raid6_recov_rvv;

extern const struct raid6_calls raid6_neonx1;
extern const struct raid6_calls raid6_neonx2;
extern const struct raid6_calls raid6_neonx4;
extern const struct raid6_calls raid6_neonx8;

/* Algorithm list */
extern const struct raid6_calls * const raid6_algos[];
extern const struct raid6_recov_calls *const raid6_recov_algos[];

/* Return values from chk_syndrome */
#define RAID6_OK	0
#define RAID6_P_BAD	1
#define RAID6_Q_BAD	2
#define RAID6_PQ_BAD	3

/* Galois field tables */
extern const u8 raid6_gfmul[256][256] __attribute__((aligned(256)));
extern const u8 raid6_vgfmul[256][32] __attribute__((aligned(256)));
extern const u8 raid6_gfexp[256]      __attribute__((aligned(256)));
extern const u8 raid6_gflog[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfinv[256]      __attribute__((aligned(256)));
extern const u8 raid6_gfexi[256]      __attribute__((aligned(256)));

/* Recovery routines */
extern void (*raid6_2data_recov)(int disks, size_t bytes, int faila, int failb,
		       void **ptrs);
extern void (*raid6_datap_recov)(int disks, size_t bytes, int faila,
			void **ptrs);

#endif /* LINUX_RAID_RAID6_H */
