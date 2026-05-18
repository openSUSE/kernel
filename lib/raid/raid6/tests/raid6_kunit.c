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

struct test_args {
	unsigned int recov_idx;
	const struct raid6_recov_calls *recov;
	unsigned int gen_idx;
	const struct raid6_calls *gen;
};

static struct test_args args;

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

static void test_recover(struct kunit *test, int faila, int failb)
{
	const struct test_args *ta = test->param_value;

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
		ta->gen->gen_syndrome(NDISKS, PAGE_SIZE, dataptrs);
	} else if (failb == NDISKS - 2) {
		/* data+P failure. */
		ta->recov->datap(NDISKS, PAGE_SIZE, faila, dataptrs);
	} else {
		/* data+data failure. */
		ta->recov->data2(NDISKS, PAGE_SIZE, faila, failb, dataptrs);
	}

	KUNIT_EXPECT_MEMEQ_MSG(test, data[faila], recovi, PAGE_SIZE,
			"faila miscompared: %3d[%c] (failb=%3d[%c])\n",
			faila, member_type(faila),
			failb, member_type(failb));
	KUNIT_EXPECT_MEMEQ_MSG(test, data[failb], recovj, PAGE_SIZE,
			"failb miscompared: %3d[%c] (faila=%3d[%c])\n",
			failb, member_type(failb),
			faila, member_type(faila));

skip:
	dataptrs[faila] = data[faila];
	dataptrs[failb] = data[failb];
}

static void raid6_test(struct kunit *test)
{
	const struct test_args *ta = test->param_value;
	int i, j, p1, p2;

	/* Nuke syndromes */
	memset(data[NDISKS - 2], 0xee, PAGE_SIZE);
	memset(data[NDISKS - 1], 0xee, PAGE_SIZE);

	/* Generate assumed good syndrome */
	ta->gen->gen_syndrome(NDISKS, PAGE_SIZE, (void **)&dataptrs);

	for (i = 0; i < NDISKS - 1; i++)
		for (j = i + 1; j < NDISKS; j++)
			test_recover(test, i, j);

	if (!ta->gen->xor_syndrome)
		return;

	for (p1 = 0; p1 < NDISKS - 2; p1++) {
		for (p2 = p1; p2 < NDISKS - 2; p2++) {
			/* Simulate rmw run */
			ta->gen->xor_syndrome(NDISKS, p1, p2, PAGE_SIZE,
					(void **)&dataptrs);
			makedata(p1, p2);
			ta->gen->xor_syndrome(NDISKS, p1, p2, PAGE_SIZE,
					(void **)&dataptrs);

			for (i = 0; i < NDISKS - 1; i++)
				for (j = i + 1; j < NDISKS; j++)
					test_recover(test, i, j);
		}
	}
}

static const void *raid6_gen_params(struct kunit *test, const void *prev,
		char *desc)
{
	if (!prev) {
		memset(&args, 0, sizeof(args));
next_algo:
		args.recov_idx = 0;
		args.gen = raid6_algo_find(args.gen_idx);
		if (!args.gen)
			return NULL;
	}

	if (args.recov)
		args.recov_idx++;
	args.recov = raid6_recov_algo_find(args.recov_idx);
	if (!args.recov) {
		args.gen_idx++;
		goto next_algo;
	}

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "gen=%s recov=%s",
			args.gen->name, args.recov->name);
	return &args;
}

static struct kunit_case raid6_test_cases[] = {
	KUNIT_CASE_PARAM(raid6_test, raid6_gen_params),
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
