#ifndef __LINUX_SPINLOCK_API_SMP_H
#define __LINUX_SPINLOCK_API_SMP_H

#ifndef __LINUX_SPINLOCK_H
# error "please don't include this file directly"
#endif

/*
 * include/linux/spinlock_api_smp.h
 *
 * spinlock API declarations on SMP (and debug)
 * (implemented in kernel/spinlock.c)
 *
 * portions Copyright 2005, Red Hat, Inc., Ingo Molnar
 * Released under the General Public License (GPL).
 */

int in_lock_functions(unsigned long addr);

#define assert_atomic_spin_locked(x)	BUG_ON(!atomic_spin_is_locked(x))

void __lockfunc
_atomic_spin_lock(atomic_spinlock_t *lock)		__acquires(lock);

void __lockfunc
_atomic_spin_lock_nested(atomic_spinlock_t *lock, int subclass)
							__acquires(lock);

void __lockfunc
_atomic_spin_lock_nest_lock(atomic_spinlock_t *lock, struct lockdep_map *map)
							__acquires(lock);
void __lockfunc
_atomic_spin_lock_bh(atomic_spinlock_t *lock)		__acquires(lock);

void __lockfunc
_atomic_spin_lock_irq(atomic_spinlock_t *lock)		__acquires(lock);

unsigned long __lockfunc
_atomic_spin_lock_irqsave(atomic_spinlock_t *lock)	__acquires(lock);

unsigned long __lockfunc
_atomic_spin_lock_irqsave_nested(atomic_spinlock_t *lock, int subclass)
							__acquires(lock);

int __lockfunc _atomic_spin_trylock(atomic_spinlock_t *lock);
int __lockfunc _atomic_spin_trylock_bh(atomic_spinlock_t *lock);

void __lockfunc
_atomic_spin_unlock(atomic_spinlock_t *lock)		__releases(lock);

void __lockfunc
_atomic_spin_unlock_bh(atomic_spinlock_t *lock)		__releases(lock);

void __lockfunc
_atomic_spin_unlock_irq(atomic_spinlock_t *lock)	__releases(lock);

void __lockfunc
_atomic_spin_unlock_irqrestore(atomic_spinlock_t *lock,	unsigned long flags)
							__releases(lock);


void __lockfunc _read_lock(rwlock_t *lock)		__acquires(lock);
void __lockfunc _write_lock(rwlock_t *lock)		__acquires(lock);
void __lockfunc _read_lock_bh(rwlock_t *lock)		__acquires(lock);
void __lockfunc _write_lock_bh(rwlock_t *lock)		__acquires(lock);
void __lockfunc _read_lock_irq(rwlock_t *lock)		__acquires(lock);
void __lockfunc _write_lock_irq(rwlock_t *lock)		__acquires(lock);

unsigned long __lockfunc _read_lock_irqsave(rwlock_t *lock)
							__acquires(lock);
unsigned long __lockfunc _write_lock_irqsave(rwlock_t *lock)
							__acquires(lock);
int __lockfunc _read_trylock(rwlock_t *lock);
int __lockfunc _write_trylock(rwlock_t *lock);

void __lockfunc _read_unlock(rwlock_t *lock)		__releases(lock);
void __lockfunc _write_unlock(rwlock_t *lock)		__releases(lock);
void __lockfunc _read_unlock_bh(rwlock_t *lock)		__releases(lock);
void __lockfunc _write_unlock_bh(rwlock_t *lock)	__releases(lock);

void __lockfunc _read_unlock_irq(rwlock_t *lock)	__releases(lock);
void __lockfunc _write_unlock_irq(rwlock_t *lock)	__releases(lock);

void __lockfunc _read_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(lock);
void __lockfunc _write_unlock_irqrestore(rwlock_t *lock, unsigned long flags)
							__releases(lock);

#endif /* __LINUX_SPINLOCK_API_SMP_H */
