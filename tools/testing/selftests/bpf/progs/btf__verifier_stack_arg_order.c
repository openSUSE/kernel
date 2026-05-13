// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#if defined(__TARGET_ARCH_x86) && defined(__BPF_FEATURE_STACK_ARGUMENT)

int subprog_bad_order_6args(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}

int subprog_call_before_load_6args(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}

int subprog_pruning_call_before_load_6args(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}

#else

int subprog_bad_order_6args(void)
{
	return 0;
}

int subprog_call_before_load_6args(void)
{
	return 0;
}

int subprog_pruning_call_before_load_6args(void)
{
	return 0;
}

#endif
