// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include "bpf_experimental.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../bpf_testmod/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

SEC("raw_tp/sys_enter")
__failure __msg("R1 cannot write into rdonly_mem")
/* Message should not be 'R1 cannot write into rdonly_trusted_mem' */
int iter_next_ptr_mem_not_trusted(const void *ctx)
{
	struct bpf_iter_num num_it;
	int *num_ptr;

	bpf_iter_num_new(&num_it, 0, 10);

	num_ptr = bpf_iter_num_next(&num_it);
	if (num_ptr == NULL)
		goto out;

	bpf_kfunc_trusted_num_test(num_ptr);
out:
	bpf_iter_num_destroy(&num_it);
	return 0;
}
