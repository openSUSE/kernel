// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
#include "../../../arch/arm64/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_AARCH64_SUPPORT
#include <libunwind-aarch64.h>
#endif

int __get_perf_regnum_for_unw_regnum_arm64(int unw_regnum)
{
	if (unw_regnum < 0 || unw_regnum >= PERF_REG_ARM64_EXTENDED_MAX) {
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
	return unw_regnum;
}

void __libunwind_arch__flush_access_arm64(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_AARCH64_SUPPORT
	unw_flush_cache(maps__addr_space(maps), 0, 0);
#endif
}

void __libunwind_arch__finish_access_arm64(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_AARCH64_SUPPORT
	unw_destroy_addr_space(maps__addr_space(maps));
#endif
}
