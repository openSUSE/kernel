// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
#include "../../../arch/s390/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
#include <libunwind-s390x.h>
#endif

int __get_perf_regnum_for_unw_regnum_s390(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_S390X_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_S390X_R0 ... UNW_S390X_R15:
		return unw_regnum - UNW_S390X_R0 + PERF_REG_S390_R0;
	case UNW_S390X_F0 ... UNW_S390X_F15:
		return unw_regnum - UNW_S390X_F0 + PERF_REG_S390_FP0;
	case UNW_S390X_IP:
		return PERF_REG_S390_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
#endif // HAVE_LIBUNWIND_S390X_SUPPORT
}

void __libunwind_arch__flush_access_s390(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	unw_flush_cache(maps__addr_space(maps), 0, 0);
#endif
}

void __libunwind_arch__finish_access_s390(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_S390X_SUPPORT
	unw_destroy_addr_space(maps__addr_space(maps));
#endif
}
