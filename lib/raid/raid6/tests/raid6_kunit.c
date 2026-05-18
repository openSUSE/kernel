// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2002-2007 H. Peter Anvin - All Rights Reserved
 *
 * Test RAID-6 recovery algorithms.
 */

#include <kunit/test.h>
#include <linux/prandom.h>
#include "../algos.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

#define RAID6_KUNIT_SEED		42

#define NDISKS		16	/* Including P and Q */

static struct rnd_state rng;
static void *dataptrs[NDISKS];
static char data[NDISKS][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static char recovi[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static char recovj[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static void makedata(int start, int stop)
{
	int i;

	for (i = start; i <= stop; i++) {
		prandom_bytes_state(&rng, data[i], PAGE_SIZE);
		dataptrs[i] = data[i];
	}
}

static char member_type(int d)
{
	switch (d) {
	case NDISKS-2:
		return 'P';
	case NDISKS-1:
		return 'Q';
	default:
		return 'D';
	}
}

static void test_disks(struct kunit *test, const struct raid6_calls *calls,
		const struct raid6_recov_calls *ra, int faila, int failb)
{
	memset(recovi, 0xf0, PAGE_SIZE);
	memset(recovj, 0xba, PAGE_SIZE);

	dataptrs[faila] = recovi;
	dataptrs[failb] = recovj;

	if (failb == NDISKS - 1) {
		/*
		 * We don't implement the data+Q failure scenario, since it
		 * is equivalent to a RAID-5 failure (XOR, then recompute Q).
		 */
		if (faila != NDISKS - 2)
			goto skip;

		/* P+Q failure.  Just rebuild the syndrome. */
		calls->gen_syndrome(NDISKS, PAGE_SIZE, dataptrs);
	} else if (failb == NDISKS - 2) {
		/* data+P failure. */
		ra->datap(NDISKS, PAGE_SIZE, faila, dataptrs);
	} else {
		/* data+data failure. */
		ra->data2(NDISKS, PAGE_SIZE, faila, failb, dataptrs);
	}

	KUNIT_EXPECT_MEMEQ_MSG(test, data[faila], recovi, PAGE_SIZE,
		"algo=%-8s/%-8s faila miscompared: %3d[%c] (failb=%3d[%c])\n",
	       calls->name, ra->name,
	       faila, member_type(faila),
	       failb, member_type(failb));
	KUNIT_EXPECT_MEMEQ_MSG(test, data[failb], recovj, PAGE_SIZE,
		"algo=%-8s/%-8s failb miscompared: %3d[%c] (faila=%3d[%c])\n",
	       calls->name, ra->name,
	       failb, member_type(failb),
	       faila, member_type(faila));

skip:
	dataptrs[faila] = data[faila];
	dataptrs[failb] = data[failb];
}

static void raid6_test(struct kunit *test)
{
	int i, j, p1, p2;
	unsigned int r, g;

	for (r = 0; ; r++) {
		const struct raid6_recov_calls *ra = raid6_recov_algo_find(r);

		if (!ra)
			break;

		for (g = 0; ; g++) {
			const struct raid6_calls *calls = raid6_algo_find(g);

			if (!calls)
				break;

			/* Nuke syndromes */
			memset(data[NDISKS - 2], 0xee, PAGE_SIZE);
			memset(data[NDISKS - 1], 0xee, PAGE_SIZE);

			/* Generate assumed good syndrome */
			calls->gen_syndrome(NDISKS, PAGE_SIZE,
						(void **)&dataptrs);

			for (i = 0; i < NDISKS-1; i++)
				for (j = i+1; j < NDISKS; j++)
					test_disks(test, calls, ra, i, j);

			if (!calls->xor_syndrome)
				continue;

			for (p1 = 0; p1 < NDISKS-2; p1++)
				for (p2 = p1; p2 < NDISKS-2; p2++) {

					/* Simulate rmw run */
					calls->xor_syndrome(NDISKS, p1, p2, PAGE_SIZE,
								(void **)&dataptrs);
					makedata(p1, p2);
					calls->xor_syndrome(NDISKS, p1, p2, PAGE_SIZE,
                                                                (void **)&dataptrs);

					for (i = 0; i < NDISKS-1; i++)
						for (j = i+1; j < NDISKS; j++)
							test_disks(test, calls,
									ra, i, j);
				}

		}
	}
}

static struct kunit_case raid6_test_cases[] = {
	KUNIT_CASE(raid6_test),
	{},
};

static int raid6_suite_init(struct kunit_suite *suite)
{
	prandom_seed_state(&rng, RAID6_KUNIT_SEED);
	makedata(0, NDISKS - 1);
	return 0;
}

static struct kunit_suite raid6_test_suite = {
	.name		= "raid6",
	.test_cases	= raid6_test_cases,
	.suite_init	= raid6_suite_init,
};
kunit_test_suite(raid6_test_suite);

MODULE_DESCRIPTION("Unit test for the RAID P/Q library functions");
MODULE_LICENSE("GPL");
