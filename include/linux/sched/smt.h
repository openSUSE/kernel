#ifndef _LINUX_SCHED_SMT_H
#define _LINUX_SCHED_SMT_H

#ifdef CONFIG_SCHED_SMT
extern bool sched_smt_present;

static __always_inline bool sched_smt_active(void)
{
	return sched_smt_present;
}
#else
static inline bool sched_smt_active(void) { return false; }
#endif

void arch_smt_update(void);

#endif /* _LINUX_SCHED_SMT_H */

