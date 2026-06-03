/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_PPC_ENTRY_COMMON_H
#define _ASM_PPC_ENTRY_COMMON_H

#include <asm/cputime.h>
#include <asm/interrupt.h>
#include <asm/stacktrace.h>
#include <asm/tm.h>

static __always_inline void booke_load_dbcr0(void)
{
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	unsigned long dbcr0 = current->thread.debug.dbcr0;

	if (likely(!(dbcr0 & DBCR0_IDM)))
		return;

	/*
	 * Check to see if the dbcr0 register is set up to debug.
	 * Use the internal debug mode bit to do this.
	 */
	mtmsr(mfmsr() & ~MSR_DE);
	if (IS_ENABLED(CONFIG_PPC32)) {
		isync();
		global_dbcr0[smp_processor_id()] = mfspr(SPRN_DBCR0);
	}
	mtspr(SPRN_DBCR0, dbcr0);
	mtspr(SPRN_DBSR, -1);
#endif
}

static __always_inline void arch_enter_from_user_mode(struct pt_regs *regs)
{
	kuap_lock();

	if (IS_ENABLED(CONFIG_PPC_IRQ_SOFT_MASK_DEBUG))
		BUG_ON(irq_soft_mask_return() != IRQS_ALL_DISABLED);

	BUG_ON(regs_is_unrecoverable(regs));
	BUG_ON(!user_mode(regs));
	BUG_ON(regs_irqs_disabled(regs));

#ifdef CONFIG_PPC_PKEY
	if (mmu_has_feature(MMU_FTR_PKEY) && trap_is_syscall(regs)) {
		unsigned long amr, iamr;
		bool flush_needed = false;
		/*
		 * When entering from userspace we mostly have the AMR/IAMR
		 * different from kernel default values. Hence don't compare.
		 */
		amr = mfspr(SPRN_AMR);
		iamr = mfspr(SPRN_IAMR);
		regs->amr  = amr;
		regs->iamr = iamr;
		if (mmu_has_feature(MMU_FTR_KUAP)) {
			mtspr(SPRN_AMR, AMR_KUAP_BLOCKED);
			flush_needed = true;
		}
		if (mmu_has_feature(MMU_FTR_BOOK3S_KUEP)) {
			mtspr(SPRN_IAMR, AMR_KUEP_BLOCKED);
			flush_needed = true;
		}
		if (flush_needed)
			isync();
	}
#endif
	kuap_assert_locked();
	booke_restore_dbcr0();
	account_cpu_user_entry();
	account_stolen_time();

	/*
	 * This is not required for the syscall exit path, but makes the
	 * stack frame look nicer. If this was initialised in the first stack
	 * frame, or if the unwinder was taught the first stack frame always
	 * returns to user with IRQS_ENABLED, this store could be avoided!
	 */
	irq_soft_mask_regs_set_state(regs, IRQS_ENABLED);

	/*
	 * If system call is called with TM active, set _TIF_RESTOREALL to
	 * prevent RFSCV being used to return to userspace, because POWER9
	 * TM implementation has problems with this instruction returning to
	 * transactional state. Final register values are not relevant because
	 * the transaction will be aborted upon return anyway. Or in the case
	 * of unsupported_scv SIGILL fault, the return state does not much
	 * matter because it's an edge case.
	 */
	if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM) &&
	    unlikely(MSR_TM_TRANSACTIONAL(regs->msr)))
		set_bits(_TIF_RESTOREALL, &current_thread_info()->flags);

	/*
	 * If the system call was made with a transaction active, doom it and
	 * return without performing the system call. Unless it was an
	 * unsupported scv vector, in which case it's treated like an illegal
	 * instruction.
	 */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (unlikely(MSR_TM_TRANSACTIONAL(regs->msr)) &&
	    !trap_is_unsupported_scv(regs)) {
		/* Enable TM in the kernel, and disable EE (for scv) */
		hard_irq_disable();
		mtmsr(mfmsr() | MSR_TM);

		/* tabort, this dooms the transaction, nothing else */
		asm volatile(".long 0x7c00071d | ((%0) << 16)"
			     :: "r"(TM_CAUSE_SYSCALL | TM_CAUSE_PERSISTENT));

		/*
		 * Userspace will never see the return value. Execution will
		 * resume after the tbegin. of the aborted transaction with the
		 * checkpointed register state. A context switch could occur
		 * or signal delivered to the process before resuming the
		 * doomed transaction context, but that should all be handled
		 * as expected.
		 */
		return;
	}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */
}

#define arch_enter_from_user_mode arch_enter_from_user_mode

#endif /* _ASM_PPC_ENTRY_COMMON_H */
