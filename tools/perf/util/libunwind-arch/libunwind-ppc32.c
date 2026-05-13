// SPDX-License-Identifier: GPL-2.0-or-later
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
#include "../../../arch/powerpc/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_PPC32_SUPPORT
#include <libunwind-ppc32.h>
#endif

int __get_perf_regnum_for_unw_regnum_ppc32(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_PPC32_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_PPC32_R0 ... UNW_PPC32_R31:
		return unw_regnum - UNW_PPC32_R0 + PERF_REG_POWERPC_R0;
	case UNW_PPC32_LR:
		return PERF_REG_POWERPC_LINK;
	case UNW_PPC32_CTR:
		return PERF_REG_POWERPC_CTR;
	case UNW_PPC32_XER:
		return PERF_REG_POWERPC_XER;
	case UNW_PPC32_NIP:
		return PERF_REG_POWERPC_NIP;
	default:
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
#endif // HAVE_LIBUNWIND_PPC32_SUPPORT
}

void __libunwind_arch__flush_access_ppc32(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_PPC32_SUPPORT
	unw_flush_cache(maps__addr_space(maps), 0, 0);
#endif
}

void __libunwind_arch__finish_access_ppc32(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_PPC32_SUPPORT
	unw_destroy_addr_space(maps__addr_space(maps));
#endif
}
