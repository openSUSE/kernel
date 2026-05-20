/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_COREDUMP_H
#define _LINUX_SCHED_COREDUMP_H

#include <linux/mm_types.h>

/*
 * Task dumpability mode.  Gates core dump production and ptrace_attach()
 * authorization.  The numeric values are stable ABI (suid_dumpable
 * sysctl, prctl(PR_SET_DUMPABLE)); do not renumber.
 */
enum task_dumpable {
	TASK_DUMPABLE_OFF	= 0,	/* no dump; ptrace needs CAP_SYS_PTRACE */
	TASK_DUMPABLE_OWNER	= 1,	/* default; dump and ptrace by uid match */
	TASK_DUMPABLE_ROOT	= 2,	/* dump as root; ptrace needs CAP_SYS_PTRACE */
};

static inline unsigned long __mm_flags_get_dumpable(const struct mm_struct *mm)
{
	/*
	 * By convention, dumpable bits are contained in first 32 bits of the
	 * bitmap, so we can simply access this first unsigned long directly.
	 */
	return __mm_flags_get_word(mm);
}

static inline void __mm_flags_set_mask_dumpable(struct mm_struct *mm, int value)
{
	__mm_flags_set_mask_bits_word(mm, MMF_DUMPABLE_MASK, value);
}

extern void set_dumpable(struct mm_struct *mm, int value);
/*
 * This returns the actual value of the suid_dumpable flag. For things
 * that are using this for checking for privilege transitions, it must
 * test against TASK_DUMPABLE_OWNER rather than treating it as a boolean
 * value.
 */
static inline int __get_dumpable(unsigned long mm_flags)
{
	return mm_flags & MMF_DUMPABLE_MASK;
}

static inline int get_dumpable(struct mm_struct *mm)
{
	unsigned long flags = __mm_flags_get_dumpable(mm);

	return __get_dumpable(flags);
}

#endif /* _LINUX_SCHED_COREDUMP_H */
