// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

__noinline __used __naked
static int subprog_bad_order_6args(int a, int b, int c, int d, int e, int f)
{
	asm volatile (
		"*(u64 *)(r11 - 8) = r1;"
		"r0 = *(u64 *)(r11 + 8);"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: r11 load after r11 store")
__failure
__msg("r11 load must be before any r11 store or call insn")
__btf_func_path("btf__verifier_stack_arg_order.bpf.o")
__naked void stack_arg_load_after_store(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 8) = 6;"
		"call subprog_bad_order_6args;"
		"exit;"
		::: __clobber_all
	);
}

__noinline __used __naked
static int subprog_call_before_load_6args(int a, int b, int c, int d, int e,
					  int f)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r0 = *(u64 *)(r11 + 8);"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: r11 load after a call")
__failure
__msg("r11 load must be before any r11 store or call insn")
__btf_func_path("btf__verifier_stack_arg_order.bpf.o")
__naked void stack_arg_load_after_call(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 8) = 6;"
		"call subprog_call_before_load_6args;"
		"exit;"
		::: __clobber_all
	);
}

__noinline __used __naked
static int subprog_pruning_call_before_load_6args(int a, int b, int c, int d,
						  int e, int f)
{
	asm volatile (
		"if r1 s> 0 goto l0_%=;"
		"goto l1_%=;"
	"l0_%=:"
		"call %[bpf_get_prandom_u32];"
	"l1_%=:"
		"r0 = *(u64 *)(r11 + 8);"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: pruning keeps r11 load ordering")
__failure
__flag(BPF_F_TEST_STATE_FREQ)
__msg("r11 load must be before any r11 store or call insn")
__btf_func_path("btf__verifier_stack_arg_order.bpf.o")
__naked void stack_arg_pruning_load_after_call(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r1 = r0;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 8) = 6;"
		"call subprog_pruning_call_before_load_6args;"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

#else

SEC("socket")
__description("stack_arg order is not supported by compiler or jit, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
