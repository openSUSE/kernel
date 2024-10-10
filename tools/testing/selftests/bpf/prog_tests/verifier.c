// SPDX-License-Identifier: GPL-2.0-only

#include <test_progs.h>

#include "cap_helpers.h"
#include "verifier_const.skel.h"
#include "verifier_iterating_callbacks.skel.h"
#include "verifier_reg_equal.skel.h"
#include "verifier_scalar_ids.skel.h"
#include "verifier_sockmap_mutate.skel.h"
#include "verifier_subprog_precision.skel.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

__maybe_unused
static void run_tests_aux(const char *skel_name,
			  skel_elf_bytes_fn elf_bytes_factory,
			  pre_execution_cb pre_execution_cb)
{
	struct test_loader tester = {};
	__u64 old_caps;
	int err;

	/* test_verifier tests are executed w/o CAP_SYS_ADMIN, do the same here */
	err = cap_disable_effective(1ULL << CAP_SYS_ADMIN, &old_caps);
	if (err) {
		PRINT_FAIL("failed to drop CAP_SYS_ADMIN: %i, %s\n", err, strerror(err));
		return;
	}

	test_loader__set_pre_execution_cb(&tester, pre_execution_cb);
	test_loader__run_subtests(&tester, skel_name, elf_bytes_factory);
	test_loader_fini(&tester);

	err = cap_enable_effective(old_caps, NULL);
	if (err)
		PRINT_FAIL("failed to restore CAP_SYS_ADMIN: %i, %s\n", err, strerror(err));
}

#define RUN(skel) run_tests_aux(#skel, skel##__elf_bytes, NULL)

void test_verifier_const(void)                { RUN(verifier_const); }
void test_verifier_iterating_callbacks(void)  { RUN(verifier_iterating_callbacks); }
void test_verifier_reg_equal(void)            { RUN(verifier_reg_equal); }
void test_verifier_scalar_ids(void)           { RUN(verifier_scalar_ids); }
void test_verifier_sockmap_mutate(void)       { RUN(verifier_sockmap_mutate); }
void test_verifier_subprog_precision(void)    { RUN(verifier_subprog_precision); }
