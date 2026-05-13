/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBUNWIND_ARCH_H
#define __LIBUNWIND_ARCH_H

struct maps;

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

void __libunwind_arch__flush_access_arm(struct maps *maps);
void __libunwind_arch__flush_access_arm64(struct maps *maps);
void __libunwind_arch__flush_access_loongarch(struct maps *maps);
void __libunwind_arch__flush_access_mips(struct maps *maps);
void __libunwind_arch__flush_access_ppc32(struct maps *maps);
void __libunwind_arch__flush_access_ppc64(struct maps *maps);
void __libunwind_arch__flush_access_s390(struct maps *maps);
void __libunwind_arch__flush_access_i386(struct maps *maps);
void __libunwind_arch__flush_access_x86_64(struct maps *maps);
void libunwind_arch__flush_access(struct maps *maps);

void __libunwind_arch__finish_access_arm(struct maps *maps);
void __libunwind_arch__finish_access_arm64(struct maps *maps);
void __libunwind_arch__finish_access_loongarch(struct maps *maps);
void __libunwind_arch__finish_access_mips(struct maps *maps);
void __libunwind_arch__finish_access_ppc32(struct maps *maps);
void __libunwind_arch__finish_access_ppc64(struct maps *maps);
void __libunwind_arch__finish_access_s390(struct maps *maps);
void __libunwind_arch__finish_access_i386(struct maps *maps);
void __libunwind_arch__finish_access_x86_64(struct maps *maps);
void libunwind_arch__finish_access(struct maps *maps);

#endif /* __LIBUNWIND_ARCH_H */
