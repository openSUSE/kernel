// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include <elf.h>
#include <errno.h>

int get_perf_regnum_for_unw_regnum(unsigned int e_machine, int unw_regnum)
{
	switch (e_machine) {
	case EM_ARM:
		return __get_perf_regnum_for_unw_regnum_arm(unw_regnum);
	case EM_AARCH64:
		return __get_perf_regnum_for_unw_regnum_arm64(unw_regnum);
	case EM_LOONGARCH:
		return __get_perf_regnum_for_unw_regnum_loongarch(unw_regnum);
	case EM_MIPS:
		return __get_perf_regnum_for_unw_regnum_mips(unw_regnum);
	case EM_PPC:
		return __get_perf_regnum_for_unw_regnum_ppc32(unw_regnum);
	case EM_PPC64:
		return __get_perf_regnum_for_unw_regnum_ppc64(unw_regnum);
	case EM_S390:
		return __get_perf_regnum_for_unw_regnum_s390(unw_regnum);
	case EM_386:
		return __get_perf_regnum_for_unw_regnum_i386(unw_regnum);
	case EM_X86_64:
		return __get_perf_regnum_for_unw_regnum_x86_64(unw_regnum);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		return -EINVAL;
	}
}
