// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
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


void libunwind_arch__flush_access(struct maps *maps)
{
	unsigned int e_machine = maps__e_machine(maps);

	switch (e_machine) {
	case EM_NONE:
		break;  // No libunwind info on the maps.
	case EM_ARM:
		__libunwind_arch__flush_access_arm(maps);
		break;
	case EM_AARCH64:
		__libunwind_arch__flush_access_arm64(maps);
		break;
	case EM_LOONGARCH:
		__libunwind_arch__flush_access_loongarch(maps);
		break;
	case EM_MIPS:
		__libunwind_arch__flush_access_mips(maps);
		break;
	case EM_PPC:
		__libunwind_arch__flush_access_ppc32(maps);
		break;
	case EM_PPC64:
		__libunwind_arch__flush_access_ppc64(maps);
		break;
	case EM_S390:
		__libunwind_arch__flush_access_s390(maps);
		break;
	case EM_386:
		__libunwind_arch__flush_access_i386(maps);
		break;
	case EM_X86_64:
		__libunwind_arch__flush_access_x86_64(maps);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		break;
	}
}

void libunwind_arch__finish_access(struct maps *maps)
{
	unsigned int e_machine = maps__e_machine(maps);

	switch (e_machine) {
	case EM_NONE:
		break;  // No libunwind info on the maps.
	case EM_ARM:
		__libunwind_arch__finish_access_arm(maps);
		break;
	case EM_AARCH64:
		__libunwind_arch__finish_access_arm64(maps);
		break;
	case EM_LOONGARCH:
		__libunwind_arch__finish_access_loongarch(maps);
		break;
	case EM_MIPS:
		__libunwind_arch__finish_access_mips(maps);
		break;
	case EM_PPC:
		__libunwind_arch__finish_access_ppc32(maps);
		break;
	case EM_PPC64:
		__libunwind_arch__finish_access_ppc64(maps);
		break;
	case EM_S390:
		__libunwind_arch__finish_access_s390(maps);
		break;
	case EM_386:
		__libunwind_arch__finish_access_i386(maps);
		break;
	case EM_X86_64:
		__libunwind_arch__finish_access_x86_64(maps);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		break;
	}
}
