#ifndef _LINUX_SCHED_SMT_H
#define _LINUX_SCHED_SMT_H

#ifdef CONFIG_SCHED_SMT
extern atomic_t sched_smt_present;

static __always_inline int sched_smt_active(void)
{
	return atomic_read(&sched_smt_present);
}
#else
static inline int sched_smt_active(void) { return 0; }
#endif

void arch_smt_update(void);

#endif /* _LINUX_SCHED_SMT_H */

