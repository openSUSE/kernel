/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2003 H. Peter Anvin - All Rights Reserved
 */
#ifndef _PQ_IMPL_H
#define _PQ_IMPL_H

#include <linux/raid/pq_tables.h>

/* Routine choices */
struct raid6_calls {
	const char *name;
	void (*gen_syndrome)(int disks, size_t bytes, void **ptrs);
	void (*xor_syndrome)(int disks, int start, int stop, size_t bytes,
			void **ptrs);
	int  (*valid)(void);	/* Returns 1 if this routine set is usable */
	int priority;		/* Relative priority ranking if non-zero */
};

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
	const char *name;
	void (*data2)(int disks, size_t bytes, int faila, int failb,
			void **ptrs);
	void (*datap)(int disks, size_t bytes, int faila, void **ptrs);
	int  (*valid)(void);
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

#endif /* _PQ_IMPL_H */
