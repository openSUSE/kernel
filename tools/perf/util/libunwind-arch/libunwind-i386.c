// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../../../arch/x86/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_X86_SUPPORT
#include <libunwind-x86.h>
#endif

int __get_perf_regnum_for_unw_regnum_i386(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_X86_SUPPORT
	return -EINVAL;
#else
	static const int perf_i386_regnums[] = {
#define REGNUM(reg) [UNW_X86_E ## reg] = PERF_REG_X86_ ## reg
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
	};

	if (unw_regnum == UNW_X86_EAX)
		return PERF_REG_X86_AX;

	if (unw_regnum <  0 || unw_regnum >= (int)ARRAY_SIZE(perf_i386_regnums) ||
	    perf_i386_regnums[unw_regnum] == 0) {
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}

	return perf_i386_regnums[unw_regnum];
#endif // HAVE_LIBUNWIND_X86_SUPPORT
}
