// SPDX-License-Identifier: GPL-2.0-or-later
#include "libunwind-arch.h"
#include "../debug.h"
#include "../../../arch/powerpc/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_PPC64_SUPPORT
#include <libunwind-ppc64.h>
#endif

int __get_perf_regnum_for_unw_regnum_ppc64(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_PPC64_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_PPC64_R0 ... UNW_PPC64_R31:
		return unw_regnum - UNW_PPC64_R0 + PERF_REG_POWERPC_R0;
	case UNW_PPC64_LR:
		return PERF_REG_POWERPC_LINK;
	case UNW_PPC64_CTR:
		return PERF_REG_POWERPC_CTR;
	case UNW_PPC64_XER:
		return PERF_REG_POWERPC_XER;
	case UNW_PPC64_NIP:
		return PERF_REG_POWERPC_NIP;
	default:
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
#endif // HAVE_LIBUNWIND_PPC64_SUPPORT
}
