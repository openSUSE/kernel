/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_PARAVIRT_H
#define _ASM_POWERPC_PARAVIRT_H

#include <linux/jump_label.h>
#include <asm/smp.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/hvcall.h>
#endif

#ifdef CONFIG_PPC_SPLPAR
#include <linux/smp.h>
#include <asm/kvm_guest.h>
#include <asm/cputhreads.h>

DECLARE_STATIC_KEY_FALSE(shared_processor);

static inline bool is_shared_processor(void)
{
	return static_branch_unlikely(&shared_processor);
}

/* If bit 0 is set, the cpu has been ceded, conferred, or preempted */
static inline u32 yield_count_of(int cpu)
{
	__be32 yield_count = READ_ONCE(lppaca_of(cpu).yield_count);
	return be32_to_cpu(yield_count);
}

static inline void yield_to_preempted(int cpu, u32 yield_count)
{
	plpar_hcall_norets(H_CONFER, get_hard_smp_processor_id(cpu), yield_count);
}
#else
static inline bool is_shared_processor(void)
{
	return false;
}

static inline u32 yield_count_of(int cpu)
{
	return 0;
}

extern void ___bad_yield_to_preempted(void);
static inline void yield_to_preempted(int cpu, u32 yield_count)
{
	___bad_yield_to_preempted(); /* This would be a bug */
}
#endif

#define vcpu_is_preempted vcpu_is_preempted
static inline bool vcpu_is_preempted(int cpu)
{
	/*
	 * The dispatch/yield bit alone is an imperfect indicator of
	 * whether the hypervisor has dispatched @cpu to run on a physical
	 * processor. When it is clear, @cpu is definitely not preempted.
	 * But when it is set, it means only that it *might* be, subject to
	 * other conditions. So we check other properties of the VM and
	 * @cpu first, resorting to the yield count last.
	 */

	/*
	 * Hypervisor preemption isn't possible in dedicated processor
	 * mode by definition.
	 */
	if (!is_shared_processor())
		return false;

#ifdef CONFIG_PPC_SPLPAR
	if (!is_kvm_guest()) {
		int first_cpu = cpu_first_thread_sibling(smp_processor_id());

		/*
		 * The PowerVM hypervisor dispatches VMs on a whole core
		 * basis. So we know that a thread sibling of the local CPU
		 * cannot have been preempted by the hypervisor, even if it
		 * has called H_CONFER, which will set the yield bit.
		 */
		if (cpu_first_thread_sibling(cpu) == first_cpu)
			return false;
	}
#endif

	if (yield_count_of(cpu) & 1)
		return true;
	return false;
}

#endif /* _ASM_POWERPC_PARAVIRT_H */
