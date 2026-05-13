/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBUNWIND_ARCH_H
#define __LIBUNWIND_ARCH_H

int __get_perf_regnum_for_unw_regnum_arm(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_arm64(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_loongarch(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_mips(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_ppc32(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_ppc64(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_s390(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_i386(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_x86_64(int unw_regnum);
int get_perf_regnum_for_unw_regnum(unsigned int e_machine, int unw_regnum);

#endif /* __LIBUNWIND_ARCH_H */
