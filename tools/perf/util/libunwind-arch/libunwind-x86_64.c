// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
#include "../../../arch/x86/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_X86_64_SUPPORT
#include <libunwind-x86_64.h>
#endif

int __get_perf_regnum_for_unw_regnum_x86_64(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_X86_64_SUPPORT
	return -EINVAL;
#else
	static const int perf_x86_64_regnums[] = {
#define REGNUM(reg) [UNW_X86_64_R ## reg] = PERF_REG_X86_ ## reg
		REGNUM(AX),
		REGNUM(DX),
		REGNUM(CX),
		REGNUM(BX),
		REGNUM(SI),
		REGNUM(DI),
		REGNUM(BP),
		REGNUM(SP),
		REGNUM(IP),
#undef REGNUM
#define REGNUM(reg) [UNW_X86_64_ ## reg] = PERF_REG_X86_ ## reg
		REGNUM(R8),
		REGNUM(R9),
		REGNUM(R10),
		REGNUM(R11),
		REGNUM(R12),
		REGNUM(R13),
		REGNUM(R14),
		REGNUM(R15),
#undef REGNUM
	};

	if (unw_regnum == UNW_X86_64_RAX)
		return PERF_REG_X86_AX;

	if (unw_regnum <  0 || unw_regnum >= (int)ARRAY_SIZE(perf_x86_64_regnums) ||
            perf_x86_64_regnums[unw_regnum] == 0) {
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
	return perf_x86_64_regnums[unw_regnum];
#endif // HAVE_LIBUNWIND_X86_64_SUPPORT
}

void __libunwind_arch__flush_access_x86_64(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_X86_64_SUPPORT
	unw_flush_cache(maps__addr_space(maps), 0, 0);
#endif
}

void __libunwind_arch__finish_access_x86_64(struct maps *maps __maybe_unused)
{
#ifdef HAVE_LIBUNWIND_X86_64_SUPPORT
	unw_destroy_addr_space(maps__addr_space(maps));
#endif
}
