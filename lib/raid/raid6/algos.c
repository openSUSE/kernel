// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6/algos.c
 *
 * Algorithm list and algorithm selection for RAID-6
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/raid/pq.h>
#include <kunit/visibility.h>
#include "algos.h"

static const struct raid6_recov_calls *raid6_recov_algo;

/* Selected algorithm */
static struct raid6_calls raid6_call;

/**
 * raid6_gen_syndrome - generate RAID6 P/Q parity
 * @disks:	number of "disks" to operate on including parity
 * @bytes:	length in bytes of each vector
 * @ptrs:	@disks size array of memory pointers
 *
 * Generate @bytes worth of RAID6 P and Q parity in @ptrs[@disks - 2] and
 * @ptrs[@disks - 1] respectively from the memory pointed to by @ptrs[0] to
 * @ptrs[@disks - 3].
 *
 * @disks must be at least 4, and the memory pointed to by each member of @ptrs
 * must be at least 64-byte aligned.  @bytes must be non-zero and a multiple of
 * 512.
 *
 * See https://kernel.org/pub/linux/kernel/people/hpa/raid6.pdf for underlying
 * algorithm.
 */
void raid6_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(disks < RAID6_MIN_DISKS);

	raid6_call.gen_syndrome(disks, bytes, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_gen_syndrome);

/**
 * raid6_xor_syndrome - update RAID6 P/Q parity
 * @disks:	number of "disks" to operate on including parity
 * @start:	first index into @disk to update
 * @stop:	last index into @disk to update
 * @bytes:	length in bytes of each vector
 * @ptrs:	@disks size array of memory pointers
 *
 * Update @bytes worth of RAID6 P and Q parity in @ptrs[@disks - 2] and
 * @ptrs[@disks - 1] respectively for the memory pointed to by
 * @ptrs[@start..@stop].
 *
 * This is used to update parity in place using the following sequence:
 *
 * 1) call raid6_xor_syndrome(disk, start, stop, ...) for the existing data.
 * 2) update the the data in @ptrs[@start..@stop].
 * 3) call raid6_xor_syndrome(disk, start, stop, ...) for the new data.
 *
 * Data between @start and @stop that is not changed should be filled
 * with a pointer to the kernel zero page.
 *
 * @disks must be at least 4, and the memory pointed to by each member of @ptrs
 * must be at least 64-byte aligned.  @bytes must be non-zero and a multiple of
 * 512.  @stop must be larger or equal to @start.
 */
void raid6_xor_syndrome(int disks, int start, int stop, size_t bytes,
		void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(disks < RAID6_MIN_DISKS);
	WARN_ON_ONCE(stop < start);

	raid6_call.xor_syndrome(disks, start, stop, bytes, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_xor_syndrome);

/*
 * raid6_can_xor_syndrome - check if raid6_xor_syndrome() can be used
 *
 * Returns %true if raid6_can_xor_syndrome() can be used, else %false.
 */
bool raid6_can_xor_syndrome(void)
{
	return !!raid6_call.xor_syndrome;
}
EXPORT_SYMBOL_GPL(raid6_can_xor_syndrome);

const struct raid6_calls * const raid6_algos[] = {
#if defined(__i386__) && !defined(__arch_um__)
	&raid6_avx512x2,
	&raid6_avx512x1,
	&raid6_avx2x2,
	&raid6_avx2x1,
	&raid6_sse2x2,
	&raid6_sse2x1,
	&raid6_sse1x2,
	&raid6_sse1x1,
	&raid6_mmxx2,
	&raid6_mmxx1,
#endif
#if defined(__x86_64__) && !defined(__arch_um__)
	&raid6_avx512x4,
	&raid6_avx512x2,
	&raid6_avx512x1,
	&raid6_avx2x4,
	&raid6_avx2x2,
	&raid6_avx2x1,
	&raid6_sse2x4,
	&raid6_sse2x2,
	&raid6_sse2x1,
#endif
#ifdef CONFIG_ALTIVEC
	&raid6_vpermxor8,
	&raid6_vpermxor4,
	&raid6_vpermxor2,
	&raid6_vpermxor1,
	&raid6_altivec8,
	&raid6_altivec4,
	&raid6_altivec2,
	&raid6_altivec1,
#endif
#if defined(CONFIG_S390)
	&raid6_s390vx8,
#endif
#ifdef CONFIG_KERNEL_MODE_NEON
	&raid6_neonx8,
	&raid6_neonx4,
	&raid6_neonx2,
	&raid6_neonx1,
#endif
#ifdef CONFIG_LOONGARCH
#ifdef CONFIG_CPU_HAS_LASX
	&raid6_lasx,
#endif
#ifdef CONFIG_CPU_HAS_LSX
	&raid6_lsx,
#endif
#endif
#ifdef CONFIG_RISCV_ISA_V
	&raid6_rvvx1,
	&raid6_rvvx2,
	&raid6_rvvx4,
	&raid6_rvvx8,
#endif
	&raid6_intx8,
	&raid6_intx4,
	&raid6_intx2,
	&raid6_intx1,
	NULL
};
EXPORT_SYMBOL_IF_KUNIT(raid6_algos);

/**
 * raid6_recov_2data - recover two missing data disks
 * @disks:	number of "disks" to operate on including parity
 * @bytes:	length in bytes of each vector
 * @faila:	first failed data disk index
 * @failb:	second failed data disk index
 * @ptrs:	@disks size array of memory pointers
 *
 * Rebuild @bytes of missing data in @ptrs[@faila] and @ptrs[@failb] from the
 * data in the remaining disks and the two parities pointed to by the other
 * indices between 0 and @disks - 1 in @ptrs.  @disks includes the data disks
 * and the two parities.  @faila must be smaller than @failb.
 *
 * Memory pointed to by each pointer in @ptrs must be page aligned and is
 * limited to %PAGE_SIZE.
 */
void raid6_recov_2data(int disks, size_t bytes, int faila, int failb,
		void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(bytes > PAGE_SIZE);
	WARN_ON_ONCE(failb <= faila);

	raid6_recov_algo->data2(disks, bytes, faila, failb, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_recov_2data);

/**
 * raid6_recov_datap - recover a missing data disk and missing P-parity
 * @disks:	number of "disks" to operate on including parity
 * @bytes:	length in bytes of each vector
 * @faila:	failed data disk index
 * @ptrs:	@disks size array of memory pointers
 *
 * Rebuild @bytes of missing data in @ptrs[@faila] and the missing P-parity in
 * @ptrs[@disks - 2] from the data in the remaining disks and the Q-parity
 * pointed to by the other indices between 0 and @disks - 1 in @ptrs.  @disks
 * includes the data disks and the two parities.
 *
 * Memory pointed to by each pointer in @ptrs must be page aligned and is
 * limited to %PAGE_SIZE.
 */
void raid6_recov_datap(int disks, size_t bytes, int faila, void **ptrs)
{
	WARN_ON_ONCE(!in_task() || irqs_disabled() || softirq_count());
	WARN_ON_ONCE(bytes & 511);
	WARN_ON_ONCE(bytes > PAGE_SIZE);

	raid6_recov_algo->datap(disks, bytes, faila, ptrs);
}
EXPORT_SYMBOL_GPL(raid6_recov_datap);

const struct raid6_recov_calls *const raid6_recov_algos[] = {
#ifdef CONFIG_X86
	&raid6_recov_avx512,
	&raid6_recov_avx2,
	&raid6_recov_ssse3,
#endif
#ifdef CONFIG_S390
	&raid6_recov_s390xc,
#endif
#if defined(CONFIG_KERNEL_MODE_NEON)
	&raid6_recov_neon,
#endif
#ifdef CONFIG_LOONGARCH
#ifdef CONFIG_CPU_HAS_LASX
	&raid6_recov_lasx,
#endif
#ifdef CONFIG_CPU_HAS_LSX
	&raid6_recov_lsx,
#endif
#endif
#ifdef CONFIG_RISCV_ISA_V
	&raid6_recov_rvv,
#endif
	&raid6_recov_intx1,
	NULL
};
EXPORT_SYMBOL_IF_KUNIT(raid6_recov_algos);

#define RAID6_TIME_JIFFIES_LG2	4
#define RAID6_TEST_DISKS	8
#define RAID6_TEST_DISKS_ORDER	3

static inline const struct raid6_recov_calls *raid6_choose_recov(void)
{
	const struct raid6_recov_calls *const *algo;
	const struct raid6_recov_calls *best;

	for (best = NULL, algo = raid6_recov_algos; *algo; algo++)
		if (!best || (*algo)->priority > best->priority)
			if (!(*algo)->valid || (*algo)->valid())
				best = *algo;

	if (best) {
		raid6_recov_algo = best;

		pr_info("raid6: using %s recovery algorithm\n", best->name);
	} else
		pr_err("raid6: Yikes! No recovery algorithm found!\n");

	return best;
}

static inline const struct raid6_calls *raid6_choose_gen(
	void *(*const dptrs)[RAID6_TEST_DISKS], const int disks)
{
	unsigned long perf, bestgenperf, j0, j1;
	int start = (disks>>1)-1, stop = disks-3;	/* work on the second half of the disks */
	const struct raid6_calls *const *algo;
	const struct raid6_calls *best;

	for (bestgenperf = 0, best = NULL, algo = raid6_algos; *algo; algo++) {
		if (!best || (*algo)->priority >= best->priority) {
			if ((*algo)->valid && !(*algo)->valid())
				continue;

			if (!IS_ENABLED(CONFIG_RAID6_PQ_BENCHMARK)) {
				best = *algo;
				break;
			}

			perf = 0;

			preempt_disable();
			j0 = jiffies;
			while ((j1 = jiffies) == j0)
				cpu_relax();
			while (time_before(jiffies,
					    j1 + (1<<RAID6_TIME_JIFFIES_LG2))) {
				(*algo)->gen_syndrome(disks, PAGE_SIZE, *dptrs);
				perf++;
			}
			preempt_enable();

			if (perf > bestgenperf) {
				bestgenperf = perf;
				best = *algo;
			}
			pr_info("raid6: %-8s gen() %5ld MB/s\n", (*algo)->name,
				(perf * HZ * (disks-2)) >>
				(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2));
		}
	}

	if (!best) {
		pr_err("raid6: Yikes! No algorithm found!\n");
		goto out;
	}

	raid6_call = *best;

	if (!IS_ENABLED(CONFIG_RAID6_PQ_BENCHMARK)) {
		pr_info("raid6: skipped pq benchmark and selected %s\n",
			best->name);
		goto out;
	}

	pr_info("raid6: using algorithm %s gen() %ld MB/s\n",
		best->name,
		(bestgenperf * HZ * (disks - 2)) >>
		(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2));

	if (best->xor_syndrome) {
		perf = 0;

		preempt_disable();
		j0 = jiffies;
		while ((j1 = jiffies) == j0)
			cpu_relax();
		while (time_before(jiffies,
				   j1 + (1 << RAID6_TIME_JIFFIES_LG2))) {
			best->xor_syndrome(disks, start, stop,
					   PAGE_SIZE, *dptrs);
			perf++;
		}
		preempt_enable();

		pr_info("raid6: .... xor() %ld MB/s, rmw enabled\n",
			(perf * HZ * (disks - 2)) >>
			(20 - PAGE_SHIFT + RAID6_TIME_JIFFIES_LG2 + 1));
	}

out:
	return best;
}


/* Try to pick the best algorithm */
/* This code uses the gfmul table as convenient data set to abuse */

static int __init raid6_select_algo(void)
{
	const int disks = RAID6_TEST_DISKS;

	const struct raid6_calls *gen_best;
	const struct raid6_recov_calls *rec_best;
	char *disk_ptr, *p;
	void *dptrs[RAID6_TEST_DISKS];
	int i, cycle;

	/* prepare the buffer and fill it circularly with gfmul table */
	disk_ptr = (char *)__get_free_pages(GFP_KERNEL, RAID6_TEST_DISKS_ORDER);
	if (!disk_ptr) {
		pr_err("raid6: Yikes!  No memory available.\n");
		return -ENOMEM;
	}

	p = disk_ptr;
	for (i = 0; i < disks; i++)
		dptrs[i] = p + PAGE_SIZE * i;

	cycle = ((disks - 2) * PAGE_SIZE) / 65536;
	for (i = 0; i < cycle; i++) {
		memcpy(p, raid6_gfmul, 65536);
		p += 65536;
	}

	if ((disks - 2) * PAGE_SIZE % 65536)
		memcpy(p, raid6_gfmul, (disks - 2) * PAGE_SIZE % 65536);

	/* select raid gen_syndrome function */
	gen_best = raid6_choose_gen(&dptrs, disks);

	/* select raid recover functions */
	rec_best = raid6_choose_recov();

	free_pages((unsigned long)disk_ptr, RAID6_TEST_DISKS_ORDER);

	return gen_best && rec_best ? 0 : -EINVAL;
}

static void raid6_exit(void)
{
	do { } while (0);
}

subsys_initcall(raid6_select_algo);
module_exit(raid6_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID6 Q-syndrome calculations");
