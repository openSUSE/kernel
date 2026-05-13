// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../../../arch/loongarch/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_LOONGARCH64_SUPPORT
#include <libunwind-loongarch64.h>
#endif

int __get_perf_regnum_for_unw_regnum_loongarch(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_LOONGARCH64_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_LOONGARCH64_R1 ... UNW_LOONGARCH64_R31:
		return unw_regnum - UNW_LOONGARCH64_R1 + PERF_REG_LOONGARCH_R1;
	case UNW_LOONGARCH64_PC:
		return PERF_REG_LOONGARCH_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
#endif // HAVE_LIBUNWIND_LOONGARCH64_SUPPORT
}
