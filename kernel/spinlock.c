/*
 * Copyright (2004) Linus Torvalds
 *
 * Author: Zwane Mwaikambo <zwane@fsmlabs.com>
 *
 * Copyright (2004, 2005) Ingo Molnar
 *
 * This file contains the spinlock/rwlock implementations for the
 * SMP and the DEBUG_SPINLOCK cases. (UP-nondebug inlines them)
 *
 * Note that some architectures have special knowledge about the
 * stack frames of these functions in their profile_pc. If you
 * change anything significant here that could change the stack
 * frame contact the architecture maintainers.
 */

#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/module.h>

#include "lock-internals.h"

int __lockfunc _atomic_spin_trylock(atomic_spinlock_t *lock)
{
	preempt_disable();
	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}
	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_atomic_spin_trylock);

/*
 * If lockdep is enabled then we use the non-preemption spin-ops
 * even on CONFIG_PREEMPT, because lockdep assumes that interrupts are
 * not re-enabled during lock-acquire (which the preempt-spin-ops do):
 */
#if !defined(CONFIG_GENERIC_LOCKBREAK) || defined(CONFIG_DEBUG_LOCK_ALLOC)

unsigned long __lockfunc _atomic_spin_lock_irqsave(atomic_spinlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	/*
	 * On lockdep we dont want the hand-coded irq-enable of
	 * _raw_spin_lock_flags() code, because lockdep assumes
	 * that interrupts are not re-enabled during lock-acquire:
	 */
#ifdef CONFIG_LOCKDEP
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
#else
	_raw_spin_lock_flags(lock, &flags);
#endif
	return flags;
}
EXPORT_SYMBOL(_atomic_spin_lock_irqsave);

void __lockfunc _atomic_spin_lock_irq(atomic_spinlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_atomic_spin_lock_irq);

void __lockfunc _atomic_spin_lock_bh(atomic_spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_atomic_spin_lock_bh);

void __lockfunc _atomic_spin_lock(atomic_spinlock_t *lock)
{
	preempt_disable();
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_atomic_spin_lock);

#else /* CONFIG_PREEMPT: */

/*
 * Build preemption-friendly versions of the following
 * lock-spinning functions:
 *
 *         _atomic_spin_lock()
 *         _atomic_spin_lock_irq()
 *         _atomic_spin_lock_irqsave()
 *         _atomic_spin_lock_bh()
 */
BUILD_LOCK_OPS(atomic_spin, atomic_spinlock);

#endif /* CONFIG_PREEMPT */

#ifdef CONFIG_DEBUG_LOCK_ALLOC

void __lockfunc _atomic_spin_lock_nested(atomic_spinlock_t *lock, int subclass)
{
	preempt_disable();
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_atomic_spin_lock_nested);

unsigned long __lockfunc
_atomic_spin_lock_irqsave_nested(atomic_spinlock_t *lock, int subclass)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, _raw_spin_trylock, _raw_spin_lock,
				_raw_spin_lock_flags, &flags);
	return flags;
}
EXPORT_SYMBOL(_atomic_spin_lock_irqsave_nested);

void __lockfunc _atomic_spin_lock_nest_lock(atomic_spinlock_t *lock,
				     struct lockdep_map *nest_lock)
{
	preempt_disable();
	spin_acquire_nest(&lock->dep_map, 0, 0, nest_lock, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_spin_trylock, _raw_spin_lock);
}
EXPORT_SYMBOL(_atomic_spin_lock_nest_lock);

#endif

void __lockfunc _atomic_spin_unlock(atomic_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_atomic_spin_unlock);

void __lockfunc
_atomic_spin_unlock_irqrestore(atomic_spinlock_t *lock, unsigned long flags)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_atomic_spin_unlock_irqrestore);

void __lockfunc _atomic_spin_unlock_irq(atomic_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_atomic_spin_unlock_irq);

void __lockfunc _atomic_spin_unlock_bh(atomic_spinlock_t *lock)
{
	spin_release(&lock->dep_map, 1, _RET_IP_);
	_raw_spin_unlock(lock);
	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}
EXPORT_SYMBOL(_atomic_spin_unlock_bh);

int __lockfunc _atomic_spin_trylock_bh(atomic_spinlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	if (_raw_spin_trylock(lock)) {
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(_atomic_spin_trylock_bh);

notrace int in_lock_functions(unsigned long addr)
{
	/* Linker adds these: start and end of __lockfunc functions */
	extern char __lock_text_start[], __lock_text_end[];

	return addr >= (unsigned long)__lock_text_start
	&& addr < (unsigned long)__lock_text_end;
}
EXPORT_SYMBOL(in_lock_functions);
