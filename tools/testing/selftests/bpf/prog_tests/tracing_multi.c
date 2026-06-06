// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <bpf/btf.h>
#include <search.h>
#include "bpf/libbpf_internal.h"
#include "tracing_multi.skel.h"
#include "trace_helpers.h"

static const char * const bpf_fentry_test[] = {
	"bpf_fentry_test1",
	"bpf_fentry_test2",
	"bpf_fentry_test3",
	"bpf_fentry_test4",
	"bpf_fentry_test5",
	"bpf_fentry_test6",
	"bpf_fentry_test7",
	"bpf_fentry_test8",
	"bpf_fentry_test9",
	"bpf_fentry_test10",
};

#define FUNCS_CNT (ARRAY_SIZE(bpf_fentry_test))

static int compare(const void *ppa, const void *ppb)
{
	const char *pa = *(const char **) ppa;
	const char *pb = *(const char **) ppb;

	return strcmp(pa, pb);
}

static void tdestroy_free_nop(void *ptr)
{
}

static __u32 *get_ids(const char * const funcs[], int funcs_cnt, const char *mod)
{
	struct btf *btf, *vmlinux_btf = NULL;
	__u32 nr, type_id, cnt = 0;
	void *root = NULL;
	__u32 *ids = NULL;
	int i, err = 0;

	btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(btf, "btf__load_vmlinux_btf"))
		return NULL;

	if (mod) {
		vmlinux_btf = btf;
		btf = btf__load_module_btf(mod, vmlinux_btf);
		if (!ASSERT_OK_PTR(btf, "btf__load_module_btf")) {
			btf__free(vmlinux_btf);
			return NULL;
		}
	}

	ids = calloc(funcs_cnt, sizeof(ids[0]));
	if (!ids)
		goto out;

	/*
	 * We sort function names by name and search them
	 * below for each function.
	 */
	for (i = 0; i < funcs_cnt; i++) {
		if (!tsearch(&funcs[i], &root, compare)) {
			ASSERT_FAIL("tsearch failed");
			err = -1;
			goto error;
		}
	}

	nr = btf__type_cnt(btf);
	for (type_id = 1; type_id < nr && cnt < funcs_cnt; type_id++) {
		const struct btf_type *type;
		const char *str, ***val;
		unsigned int idx;

		type = btf__type_by_id(btf, type_id);
		if (!type) {
			err = -1;
			break;
		}

		if (BTF_INFO_KIND(type->info) != BTF_KIND_FUNC)
			continue;

		str = btf__name_by_offset(btf, type->name_off);
		if (!str) {
			err = -1;
			break;
		}

		val = tfind(&str, &root, compare);
		if (!val)
			continue;

		/*
		 * We keep pointer for each function name so we can get the original
		 * array index and have the resulting ids array matching the original
		 * function array.
		 *
		 * Doing it this way allow us to easily test the cookies support,
		 * because each cookie is attached to particular function/id.
		 */
		idx = *val - funcs;
		ids[idx] = type_id;
		cnt++;
	}

error:
	if (err) {
		free(ids);
		ids = NULL;
	}

out:
	tdestroy(root, tdestroy_free_nop);
	btf__free(vmlinux_btf);
	btf__free(btf);
	return ids;
}

static void tracing_multi_test_run(struct tracing_multi *skel)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	prog_fd = bpf_program__fd(skel->progs.test_fentry);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");

	/* extra +1 count for sleepable programs */
	ASSERT_EQ(skel->bss->test_result_fentry, FUNCS_CNT + 1, "test_result_fentry");
	ASSERT_EQ(skel->bss->test_result_fexit, FUNCS_CNT + 1, "test_result_fexit");
}

static void test_skel_api(void)
{
	struct tracing_multi *skel;
	int err;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();

	err = tracing_multi__attach(skel);
	if (!ASSERT_OK(err, "tracing_multi__attach"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void test_link_api_pattern(void)
{
	struct tracing_multi *skel;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
					"bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
					"bpf_fentry_test*", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fentry_s = bpf_program__attach_tracing_multi(skel->progs.test_fentry_s,
					"bpf_fentry_test1", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fentry_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit_s = bpf_program__attach_tracing_multi(skel->progs.test_fexit_s,
					"bpf_fentry_test1", NULL);
	if (!ASSERT_OK_PTR(skel->links.test_fexit_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
}

static void test_link_api_ids(void)
{
	LIBBPF_OPTS(bpf_tracing_multi_opts, opts);
	struct tracing_multi *skel;
	size_t cnt = FUNCS_CNT;
	__u32 *ids;

	skel = tracing_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "tracing_multi__open_and_load"))
		return;

	skel->bss->pid = getpid();

	ids = get_ids(bpf_fentry_test, cnt, NULL);
	if (!ASSERT_OK_PTR(ids, "get_ids"))
		goto cleanup;

	opts.ids = ids;
	opts.cnt = cnt;

	skel->links.test_fentry = bpf_program__attach_tracing_multi(skel->progs.test_fentry,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fentry, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit = bpf_program__attach_tracing_multi(skel->progs.test_fexit,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fexit, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	/* Only bpf_fentry_test1 is allowed for sleepable programs. */
	opts.cnt = 1;
	skel->links.test_fentry_s = bpf_program__attach_tracing_multi(skel->progs.test_fentry_s,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fentry_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	skel->links.test_fexit_s = bpf_program__attach_tracing_multi(skel->progs.test_fexit_s,
						NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.test_fexit_s, "bpf_program__attach_tracing_multi"))
		goto cleanup;

	tracing_multi_test_run(skel);

cleanup:
	tracing_multi__destroy(skel);
	free(ids);
}

void test_tracing_multi_test(void)
{
#ifndef __x86_64__
	test__skip();
	return;
#endif

	if (test__start_subtest("skel_api"))
		test_skel_api();
	if (test__start_subtest("link_api_pattern"))
		test_link_api_pattern();
	if (test__start_subtest("link_api_ids"))
		test_link_api_ids();
}
