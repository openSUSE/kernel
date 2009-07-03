/*
 * kernel/rt.c
 *
 * Real-Time Preemption Support
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * historic credit for proving that Linux spinlocks can be implemented via
 * RT-aware mutexes goes to many people: The Pmutex project (Dirk Grambow
 * and others) who prototyped it on 2.4 and did lots of comparative
 * research and analysis; TimeSys, for proving that you can implement a
 * fully preemptible kernel via the use of IRQ threading and mutexes;
 * Bill Huey for persuasively arguing on lkml that the mutex model is the
 * right one; and to MontaVista, who ported pmutexes to 2.6.
 *
 * This code is a from-scratch implementation and is not based on pmutexes,
 * but the idea of converting spinlocks to mutexes is used here too.
 *
 * lock debugging, locking tree, deadlock detection:
 *
 *  Copyright (C) 2004, LynuxWorks, Inc., Igor Manyilov, Bill Huey
 *  Released under the General Public License (GPL).
 *
 * Includes portions of the generic R/W semaphore implementation from:
 *
 *  Copyright (c) 2001   David Howells (dhowells@redhat.com).
 *  - Derived partially from idea by Andrea Arcangeli <andrea@suse.de>
 *  - Derived also from comments by Linus
 *
 * Pending ownership of locks and ownership stealing:
 *
 *  Copyright (C) 2005, Kihon Technologies Inc., Steven Rostedt
 *
 *   (also by Steven Rostedt)
 *    - Converted single pi_lock to individual task locks.
 *
 * By Esben Nielsen:
 *    Doing priority inheritance with help of the scheduler.
 *
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *  - major rework based on Esben Nielsens initial patch
 *  - replaced thread_info references by task_struct refs
 *  - removed task->pending_owner dependency
 *  - BKL drop/reacquire for semaphore style locks to avoid deadlocks
 *    in the scheduler return path as discussed with Steven Rostedt
 *
 *  Copyright (C) 2006, Kihon Technologies Inc.
 *    Steven Rostedt <rostedt@goodmis.org>
 *  - debugged and patched Thomas Gleixner's rework.
 *  - added back the cmpxchg to the rework.
 *  - turned atomic require back on for SMP.
 */

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/plist.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/hrtimer.h>

#include "rtmutex_common.h"

#ifdef CONFIG_PREEMPT_RT
/*
 * Unlock these on crash:
 */
void zap_rt_locks(void)
{
	//trace_lock_init();
}
#endif

/*
 * struct mutex functions
 */
void __mutex_init(struct mutex *lock, char *name, struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif
	__rt_mutex_init(&lock->lock, name);
}
EXPORT_SYMBOL(__mutex_init);

void __lockfunc _mutex_lock(struct mutex *lock)
{
	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	rt_mutex_lock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_lock);

int __lockfunc _mutex_lock_interruptible(struct mutex *lock)
{
	int ret;

	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	ret = rt_mutex_lock_interruptible(&lock->lock, 0);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_interruptible);

int __lockfunc _mutex_lock_killable(struct mutex *lock)
{
	int ret;

	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
	ret = rt_mutex_lock_killable(&lock->lock, 0);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_killable);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void __lockfunc _mutex_lock_nested(struct mutex *lock, int subclass)
{
	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	rt_mutex_lock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_lock_nested);

int __lockfunc _mutex_lock_interruptible_nested(struct mutex *lock, int subclass)
{
	int ret;

	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	ret = rt_mutex_lock_interruptible(&lock->lock, 0);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_interruptible_nested);

int __lockfunc _mutex_lock_killable_nested(struct mutex *lock, int subclass)
{
	int ret;

	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
	ret = rt_mutex_lock_killable(&lock->lock, 0);
	if (ret)
		mutex_release(&lock->dep_map, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(_mutex_lock_killable_nested);
#endif

int __lockfunc _mutex_trylock(struct mutex *lock)
{
	int ret = rt_mutex_trylock(&lock->lock);

	if (ret)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(_mutex_trylock);

void __lockfunc _mutex_unlock(struct mutex *lock)
{
	mutex_release(&lock->dep_map, 1, _RET_IP_);
	rt_mutex_unlock(&lock->lock);
}
EXPORT_SYMBOL(_mutex_unlock);

/*
 * rwlock_t functions
 */
int __lockfunc rt_write_trylock(rwlock_t *rwlock)
{
	int ret = rt_mutex_trylock(&rwlock->lock);

	if (ret)
		rwlock_acquire(&rwlock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(rt_write_trylock);

int __lockfunc rt_write_trylock_irqsave(rwlock_t *rwlock, unsigned long *flags)
{
	*flags = 0;
	return rt_write_trylock(rwlock);
}
EXPORT_SYMBOL(rt_write_trylock_irqsave);

int __lockfunc rt_read_trylock(rwlock_t *rwlock)
{
	struct rt_mutex *lock = &rwlock->lock;
	int ret = 1;

	/*
	 * recursive read locks succeed when current owns the lock
	 */
	if (rt_mutex_real_owner(lock) != current || !rwlock->read_depth)
		ret = rt_mutex_trylock(lock);

	if (ret) {
		rwlock->read_depth++;
		rwlock_acquire_read(&rwlock->dep_map, 0, 1, _RET_IP_);
	}

	return ret;
}
EXPORT_SYMBOL(rt_read_trylock);

void __lockfunc rt_write_lock(rwlock_t *rwlock)
{
	rwlock_acquire(&rwlock->dep_map, 0, 0, _RET_IP_);
	__rt_spin_lock(&rwlock->lock);
}
EXPORT_SYMBOL(rt_write_lock);

void __lockfunc rt_read_lock(rwlock_t *rwlock)
{
	struct rt_mutex *lock = &rwlock->lock;

	rwlock_acquire_read(&rwlock->dep_map, 0, 0, _RET_IP_);

	/*
	 * recursive read locks succeed when current owns the lock
	 */
	if (rt_mutex_real_owner(lock) != current || !rwlock->read_depth)
		__rt_spin_lock(lock);
	rwlock->read_depth++;
}

EXPORT_SYMBOL(rt_read_lock);

void __lockfunc rt_write_unlock(rwlock_t *rwlock)
{
	/* NOTE: we always pass in '1' for nested, for simplicity */
	rwlock_release(&rwlock->dep_map, 1, _RET_IP_);
	__rt_spin_unlock(&rwlock->lock);
}
EXPORT_SYMBOL(rt_write_unlock);

void __lockfunc rt_read_unlock(rwlock_t *rwlock)
{
	rwlock_release(&rwlock->dep_map, 1, _RET_IP_);

	BUG_ON(rwlock->read_depth <= 0);

	/* Release the lock only when read_depth is down to 0 */
	if (--rwlock->read_depth == 0)
		__rt_spin_unlock(&rwlock->lock);
}
EXPORT_SYMBOL(rt_read_unlock);

unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock)
{
	rt_write_lock(rwlock);

	return 0;
}
EXPORT_SYMBOL(rt_write_lock_irqsave);

unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock)
{
	rt_read_lock(rwlock);

	return 0;
}
EXPORT_SYMBOL(rt_read_lock_irqsave);

void __rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)rwlock, sizeof(*rwlock));
	lockdep_init_map(&rwlock->dep_map, name, key, 0);
#endif
	__rt_mutex_init(&rwlock->lock, name);
	rwlock->read_depth = 0;
}
EXPORT_SYMBOL(__rt_rwlock_init);

/*
 * rw_semaphores
 */

void  rt_up_write(struct rw_semaphore *rwsem)
{
	rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
	rt_mutex_unlock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_up_write);

void  rt_up_read(struct rw_semaphore *rwsem)
{
	rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
	rt_mutex_unlock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_up_read);

/*
 * downgrade a write lock into a read lock
 * - just wake up any readers at the front of the queue
 */
void  rt_downgrade_write(struct rw_semaphore *rwsem)
{
	BUG();
}
EXPORT_SYMBOL(rt_downgrade_write);

int  rt_down_write_trylock(struct rw_semaphore *rwsem)
{
	int ret = rt_mutex_trylock(&rwsem->lock);

	if (ret)
		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(rt_down_write_trylock);

void  rt_down_write(struct rw_semaphore *rwsem)
{
	rwsem_acquire(&rwsem->dep_map, 0, 0, _RET_IP_);
	rt_mutex_lock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_down_write);

void  rt_down_write_nested(struct rw_semaphore *rwsem, int subclass)
{
	rwsem_acquire(&rwsem->dep_map, subclass, 0, _RET_IP_);
	rt_mutex_lock(&rwsem->lock);
}
EXPORT_SYMBOL(rt_down_write_nested);

int  rt_down_read_trylock(struct rw_semaphore *rwsem)
{
	int ret = rt_mutex_trylock(&rwsem->lock);

	if (ret)
		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
	return ret;
}
EXPORT_SYMBOL(rt_down_read_trylock);

static void __rt_down_read(struct rw_semaphore *rwsem, int subclass)
{
	rwsem_acquire_read(&rwsem->dep_map, subclass, 0, _RET_IP_);
	rt_mutex_lock(&rwsem->lock);
}

void  rt_down_read(struct rw_semaphore *rwsem)
{
	__rt_down_read(rwsem, 0);
}
EXPORT_SYMBOL(rt_down_read);

void  rt_down_read_nested(struct rw_semaphore *rwsem, int subclass)
{
	__rt_down_read(rwsem, subclass);
}
EXPORT_SYMBOL(rt_down_read_nested);

void  __rt_rwsem_init(struct rw_semaphore *rwsem, char *name,
			      struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)rwsem, sizeof(*rwsem));
	lockdep_init_map(&rwsem->dep_map, name, key, 0);
#endif
	__rt_mutex_init(&rwsem->lock, name);
}
EXPORT_SYMBOL(__rt_rwsem_init);

/*
 * Semaphores
 */
/*
 * Linux Semaphores implemented via RT-mutexes.
 *
 * In the down() variants we use the mutex as the semaphore blocking
 * object: we always acquire it, decrease the counter and keep the lock
 * locked if we did the 1->0 transition. The next down() will then block.
 *
 * In the up() path we atomically increase the counter and do the
 * unlock if we were the one doing the 0->1 transition.
 */

static inline void __down_complete(struct semaphore *sem)
{
	int count = atomic_dec_return(&sem->count);

	if (unlikely(count > 0))
		rt_mutex_unlock(&sem->lock);
}

void  rt_down(struct semaphore *sem)
{
	rt_mutex_lock(&sem->lock);
	__down_complete(sem);
}
EXPORT_SYMBOL(rt_down);

int  rt_down_interruptible(struct semaphore *sem)
{
	int ret;

	ret = rt_mutex_lock_interruptible(&sem->lock, 0);
	if (ret)
		return ret;
	__down_complete(sem);
	return 0;
}
EXPORT_SYMBOL(rt_down_interruptible);

int rt_down_timeout(struct semaphore *sem, long jiff)
{
	struct hrtimer_sleeper t;
	struct timespec ts;
	unsigned long expires = jiffies + jiff + 1;
	int ret;

	/*
	 * rt_mutex_slowlock can use an interruptible, but this needs to
	 * be TASK_INTERRUPTIBLE. The down_timeout uses TASK_UNINTERRUPTIBLE.
	 * To handle this we loop if a signal caused the timeout and the
	 * we recalculate the new timeout.
	 * Yes Thomas, this is a hack! But we can fix it right later.
	 */
	do {
		jiffies_to_timespec(jiff, &ts);
		hrtimer_init_on_stack(&t.timer, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
		t.timer._expires = timespec_to_ktime(ts);

		ret = rt_mutex_timed_lock(&sem->lock, &t, 0);
		if (ret != -EINTR)
			break;

		/* signal occured, but the down_timeout doesn't handle them */
		jiff = expires - jiffies;

	} while (jiff > 0);

	if (!ret)
		__down_complete(sem);
	else
		ret = -ETIME;

	return ret;
}
EXPORT_SYMBOL(rt_down_timeout);

/*
 * try to down the semaphore, 0 on success and 1 on failure. (inverted)
 */
int  rt_down_trylock(struct semaphore *sem)
{
	/*
	 * Here we are a tiny bit different from ordinary Linux semaphores,
	 * because we can get 'transient' locking-failures when say a
	 * process decreases the count from 9 to 8 and locks/releases the
	 * embedded mutex internally. It would be quite complex to remove
	 * these transient failures so lets try it the simple way first:
	 */
	if (rt_mutex_trylock(&sem->lock)) {
		__down_complete(sem);
		return 0;
	}
	return 1;
}
EXPORT_SYMBOL(rt_down_trylock);

void  rt_up(struct semaphore *sem)
{
	int count;

	/*
	 * Disable preemption to make sure a highprio trylock-er cannot
	 * preempt us here and get into an infinite loop:
	 */
	preempt_disable();
	count = atomic_inc_return(&sem->count);
	/*
	 * If we did the 0 -> 1 transition then we are the ones to unlock it:
	 */
	if (likely(count == 1))
		rt_mutex_unlock(&sem->lock);
	preempt_enable();
}
EXPORT_SYMBOL(rt_up);

void  __sema_init(struct semaphore *sem, int val,
			  char *name, char *file, int line)
{
	atomic_set(&sem->count, val);
	switch (val) {
	case 0:
		__rt_mutex_init(&sem->lock, name);
		rt_mutex_lock(&sem->lock);
		break;
	default:
		__rt_mutex_init(&sem->lock, name);
		break;
	}
}
EXPORT_SYMBOL(__sema_init);
