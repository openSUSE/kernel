// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../../../arch/mips/include/uapi/asm/perf_regs.h"
#include <linux/compiler.h>
#include <errno.h>

#ifdef HAVE_LIBUNWIND_MIPS_SUPPORT
#include <libunwind-mips.h>
#endif

int __get_perf_regnum_for_unw_regnum_mips(int unw_regnum __maybe_unused)
{
#ifndef HAVE_LIBUNWIND_MIPS_SUPPORT
	return -EINVAL;
#else
	switch (unw_regnum) {
	case UNW_MIPS_R1 ... UNW_MIPS_R25:
		return unw_regnum - UNW_MIPS_R1 + PERF_REG_MIPS_R1;
	case UNW_MIPS_R28 ... UNW_MIPS_R31:
		return unw_regnum - UNW_MIPS_R28 + PERF_REG_MIPS_R28;
	case UNW_MIPS_PC:
		return PERF_REG_MIPS_PC;
	default:
		pr_err("unwind: invalid reg id %d\n", unw_regnum);
		return -EINVAL;
	}
#endif // HAVE_LIBUNWIND_MIPS_SUPPORT
}
