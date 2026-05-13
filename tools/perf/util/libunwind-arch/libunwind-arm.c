// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../../../arch/arm/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

int __get_perf_regnum_for_unw_regnum_arm(int unw_regnum)
{
	if (unw_regnum < 0 || unw_regnum >= PERF_REG_ARM_MAX) {
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
	return unw_regnum;
}
