// SPDX-License-Identifier: GPL-2.0
/*
 * P-SEAMLDR support for TDX module management features like runtime updates
 *
 * Copyright (C) 2025 Intel Corporation
 */
#define pr_fmt(fmt)	"seamldr: " fmt

#include <linux/bug.h>
#include <linux/spinlock.h>

#include <asm/cpufeature.h>
#include <asm/cpufeatures.h>
#include <asm/seamldr.h>

#include "seamcall_internal.h"

/* P-SEAMLDR SEAMCALL leaf function */
#define P_SEAMLDR_INFO			0x8000000000000000

/*
 * Serialize P-SEAMLDR calls since the hardware only allows a single CPU to
 * interact with P-SEAMLDR simultaneously. Use raw version as the calls can
 * be made with interrupts disabled, where plain spinlocks are prohibited in
 * PREEMPT_RT kernels as they become sleeping locks.
 */
static DEFINE_RAW_SPINLOCK(seamldr_lock);

static int seamldr_call(u64 fn, struct tdx_module_args *args)
{
	/*
	 * With this bug, P-SEAMLDR calls corrupt the VMCS
	 * pointer and must be avoided. This path should be
	 * unreachable since sysfs hides the ABIs.
	 */
	if (boot_cpu_has_bug(X86_BUG_SEAMRET_INVD_VMCS)) {
		WARN_ON(1);
		return -EINVAL;
	}

	guard(raw_spinlock)(&seamldr_lock);
	return seamcall_prerr(fn, args);
}

int seamldr_get_info(struct seamldr_info *seamldr_info)
{
	struct tdx_module_args args = {};

	/*
	 * Use slow_virt_to_phys() since @seamldr_info may be allocated on
	 * the stack.
	 */
	args.rcx = slow_virt_to_phys(seamldr_info);
	return seamldr_call(P_SEAMLDR_INFO, &args);
}
EXPORT_SYMBOL_FOR_MODULES(seamldr_get_info, "tdx-host");
