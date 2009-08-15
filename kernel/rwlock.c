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

#ifndef CONFIG_PREEMPT_RT

#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/module.h>

#include "lock-internals.h"

int __lockfunc _read_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_read_trylock(lock)) {
		rwlock_acquire_read(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_read_trylock);

int __lockfunc _write_trylock(rwlock_t *lock)
{
	preempt_disable();
	if (_raw_write_trylock(lock)) {
		rwlock_acquire(&lock->dep_map, 0, 1, _RET_IP_);
		return 1;
	}

	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(_write_trylock);

/*
 * If lockdep is enabled then we use the non-preemption spin-ops
 * even on CONFIG_PREEMPT, because lockdep assumes that interrupts are
 * not re-enabled during lock-acquire (which the preempt-spin-ops do):
 */
#if !defined(CONFIG_GENERIC_LOCKBREAK) || defined(CONFIG_DEBUG_LOCK_ALLOC)

void __lockfunc _read_lock(rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}
EXPORT_SYMBOL(_read_lock);

unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, _raw_read_trylock, _raw_read_lock,
			     _raw_read_lock_flags, &flags);
	return flags;
}
EXPORT_SYMBOL(_read_lock_irqsave);

void __lockfunc _read_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}
EXPORT_SYMBOL(_read_lock_irq);

void __lockfunc _read_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	rwlock_acquire_read(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_read_trylock, _raw_read_lock);
}
EXPORT_SYMBOL(_read_lock_bh);

unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
{
	unsigned long flags;

	local_irq_save(flags);
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED_FLAGS(lock, _raw_write_trylock, _raw_write_lock,
			     _raw_write_lock_flags, &flags);
	return flags;
}
EXPORT_SYMBOL(_write_lock_irqsave);

void __lockfunc _write_lock_irq(rwlock_t *lock)
{
	local_irq_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}
EXPORT_SYMBOL(_write_lock_irq);

void __lockfunc _write_lock_bh(rwlock_t *lock)
{
	local_bh_disable();
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}
EXPORT_SYMBOL(_write_lock_bh);

void __lockfunc _write_lock(rwlock_t *lock)
{
	preempt_disable();
	rwlock_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	LOCK_CONTENDED(lock, _raw_write_trylock, _raw_write_lock);
}

EXPORT_SYMBOL(_write_lock);

#else /* CONFIG_PREEMPT: */

/*
 * Build preemption-friendly versions of the following
 * lock-spinning functions:
 *
 *         _[read|write]_lock()
 *         _[read|write]_lock_irq()
 *         _[read|write]_lock_irqsave()
 *         _[read|write]_lock_bh()
 */
BUILD_LOCK_OPS(read, read, rwlock);
BUILD_LOCK_OPS(write, write, rwlock);

#endif /* CONFIG_PREEMPT */

void __lockfunc _write_unlock(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock);

void __lockfunc _read_unlock(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock);

void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock_irqrestore);

void __lockfunc _read_unlock_irq(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_read_unlock_irq);

void __lockfunc _read_unlock_bh(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_read_unlock(lock);
	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}
EXPORT_SYMBOL(_read_unlock_bh);

void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	local_irq_restore(flags);
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock_irqrestore);

void __lockfunc _write_unlock_irq(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	local_irq_enable();
	preempt_enable();
}
EXPORT_SYMBOL(_write_unlock_irq);

void __lockfunc _write_unlock_bh(rwlock_t *lock)
{
	rwlock_release(&lock->dep_map, 1, _RET_IP_);
	_raw_write_unlock(lock);
	__preempt_enable_no_resched();
	local_bh_enable_ip((unsigned long)__builtin_return_address(0));
}
EXPORT_SYMBOL(_write_unlock_bh);

#endif
