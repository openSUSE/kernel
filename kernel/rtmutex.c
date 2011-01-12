/*
 * RT-Mutexes: simple blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner.
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2005-2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *  Copyright (C) 2005 Kihon Technologies Inc., Steven Rostedt
 *  Copyright (C) 2006 Esben Nielsen
 *
 * Adaptive Spinlocks:
 *  Copyright (C) 2008 Novell, Inc., Gregory Haskins, Sven Dietrich,
 *                                   and Peter Morreale,
 * Adaptive Spinlocks simplification:
 *  Copyright (C) 2008 Red Hat, Inc., Steven Rostedt <srostedt@redhat.com>
 *
 *  See Documentation/rt-mutex-design.txt for details.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/hardirq.h>
#include <linux/semaphore.h>

#include "rtmutex_common.h"

/*
 * lock->owner state tracking:
 *
 * lock->owner holds the task_struct pointer of the owner. Bit 0
 * is used to keep track of the "lock has waiters" state.
 *
 * owner	bit0
 * NULL		0	lock is free (fast acquire possible)
 * NULL		1	lock is free and has waiters and the top waiter
 *				is going to take the lock*
 * taskpointer	0	lock is held (fast release possible)
 * taskpointer	1	lock is held and has waiters**
 *
 * The fast atomic compare exchange based acquire and release is only
 * possible when bit 0 of lock->owner is 0.
 *
 * (*) It also can be a transitional state when grabbing the lock
 * with ->wait_lock is held. To prevent any fast path cmpxchg to the lock,
 * we need to set the bit0 before looking at the lock, and the owner may be
 * NULL in this small time, hence this can be a transitional state.
 *
 * (**) There is a small time when bit 0 is set but there are no
 * waiters. This can happen when grabbing the lock in the slow path.
 * To prevent a cmpxchg of the owner releasing the lock, we need to
 * set this bit before looking at the lock.
 */

static void
rt_mutex_set_owner(struct rt_mutex *lock, struct task_struct *owner)
{
	unsigned long val = (unsigned long)owner;

	if (rt_mutex_has_waiters(lock))
		val |= RT_MUTEX_HAS_WAITERS;

	lock->owner = (struct task_struct *)val;
}

static inline void clear_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner & ~RT_MUTEX_HAS_WAITERS);
}

static void fixup_rt_mutex_waiters(struct rt_mutex *lock)
{
	if (!rt_mutex_has_waiters(lock))
		clear_rt_mutex_waiters(lock);
}

static int rt_mutex_real_waiter(struct rt_mutex_waiter *waiter)
{
	return waiter && waiter != PI_WAKEUP_INPROGRESS;
}

/*
 * We can speed up the acquire/release, if the architecture
 * supports cmpxchg and if there's no debugging state to be set up
 */
#if defined(__HAVE_ARCH_CMPXCHG) && !defined(CONFIG_DEBUG_RT_MUTEXES)
# define rt_mutex_cmpxchg(l,c,n)	(cmpxchg(&l->owner, c, n) == c)
static inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	unsigned long owner, *p = (unsigned long *) &lock->owner;

	do {
		owner = *p;
	} while (cmpxchg(p, owner, owner | RT_MUTEX_HAS_WAITERS) != owner);
}
#else
# define rt_mutex_cmpxchg(l,c,n)	(0)
static inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner | RT_MUTEX_HAS_WAITERS);
}
#endif

int pi_initialized;

/*
 * we initialize the wait_list runtime. (Could be done build-time and/or
 * boot-time.)
 */
static inline void init_lists(struct rt_mutex *lock)
{
	if (unlikely(!lock->wait_list.prio_list.prev)) {
		plist_head_init_raw(&lock->wait_list, &lock->wait_lock);
#ifdef CONFIG_DEBUG_RT_MUTEXES
		pi_initialized++;
#endif
	}
}

/*
 * Calculate task priority from the waiter list priority
 *
 * Return task->normal_prio when the waiter list is empty or when
 * the waiter is not allowed to do priority boosting
 */
int rt_mutex_getprio(struct task_struct *task)
{
	if (likely(!task_has_pi_waiters(task)))
		return task->normal_prio;

	return min(task_top_pi_waiter(task)->pi_list_entry.prio,
		   task->normal_prio);
}

/*
 * Adjust the priority of a task, after its pi_waiters got modified.
 *
 * This can be both boosting and unboosting. task->pi_lock must be held.
 */
static void __rt_mutex_adjust_prio(struct task_struct *task)
{
	int prio = rt_mutex_getprio(task);

	if (task->prio != prio)
		rt_mutex_setprio(task, prio);
}

/*
 * Adjust task priority (undo boosting). Called from the exit path of
 * rt_mutex_slowunlock() and rt_mutex_slowlock().
 *
 * (Note: We do this outside of the protection of lock->wait_lock to
 * allow the lock to be taken while or before we readjust the priority
 * of task. We do not use the spin_xx_mutex() variants here as we are
 * outside of the debug path.)
 */
static void rt_mutex_adjust_prio(struct task_struct *task)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&task->pi_lock, flags);
	__rt_mutex_adjust_prio(task);
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
}

/*
 * Max number of times we'll walk the boosting chain:
 */
int max_lock_depth = 1024;

/*
 * Adjust the priority chain. Also used for deadlock detection.
 * Decreases task's usage by one - may thus free the task.
 * Returns 0 or -EDEADLK.
 */
static int rt_mutex_adjust_prio_chain(struct task_struct *task,
				      int deadlock_detect,
				      struct rt_mutex *orig_lock,
				      struct rt_mutex_waiter *orig_waiter,
				      struct task_struct *top_task)
{
	struct rt_mutex *lock;
	struct rt_mutex_waiter *waiter, *top_waiter = orig_waiter;
	int detect_deadlock, ret = 0, depth = 0;
	unsigned long flags;

	detect_deadlock = debug_rt_mutex_detect_deadlock(orig_waiter,
							 deadlock_detect);

	/*
	 * The (de)boosting is a step by step approach with a lot of
	 * pitfalls. We want this to be preemptible and we want hold a
	 * maximum of two locks per step. So we have to check
	 * carefully whether things change under us.
	 */
 again:
	if (++depth > max_lock_depth) {
		static int prev_max;

		/*
		 * Print this only once. If the admin changes the limit,
		 * print a new message when reaching the limit again.
		 */
		if (prev_max != max_lock_depth) {
			prev_max = max_lock_depth;
			printk(KERN_WARNING "Maximum lock depth %d reached "
			       "task: %s (%d)\n", max_lock_depth,
			       top_task->comm, task_pid_nr(top_task));
		}
		put_task_struct(task);

		return deadlock_detect ? -EDEADLK : 0;
	}
 retry:
	/*
	 * Task can not go away as we did a get_task() before !
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	/*
	 * Check whether the end of the boosting chain has been
	 * reached or the state of the chain has changed while we
	 * dropped the locks.
	 */
	if (!rt_mutex_real_waiter(waiter))
		goto out_unlock_pi;

	/*
	 * Check the orig_waiter state. After we dropped the locks,
	 * the previous owner of the lock might have released the lock.
	 */
	if (orig_waiter && !rt_mutex_owner(orig_lock))
		goto out_unlock_pi;

	/*
	 * Drop out, when the task has no waiters. Note,
	 * top_waiter can be NULL, when we are in the deboosting
	 * mode!
	 */
	if (top_waiter && (!task_has_pi_waiters(task) ||
			   top_waiter != task_top_pi_waiter(task)))
		goto out_unlock_pi;

	/*
	 * When deadlock detection is off then we check, if further
	 * priority adjustment is necessary.
	 */
	if (!detect_deadlock && waiter->list_entry.prio == task->prio)
		goto out_unlock_pi;

	lock = waiter->lock;
	if (!raw_spin_trylock(&lock->wait_lock)) {
		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
		cpu_relax();
		goto retry;
	}

	/* Deadlock detection */
	if (lock == orig_lock || rt_mutex_owner(lock) == top_task) {
		debug_rt_mutex_deadlock(deadlock_detect, orig_waiter, lock);
		raw_spin_unlock(&lock->wait_lock);
		ret = deadlock_detect ? -EDEADLK : 0;
		goto out_unlock_pi;
	}

	top_waiter = rt_mutex_top_waiter(lock);

	/* Requeue the waiter */
	plist_del(&waiter->list_entry, &lock->wait_list);
	waiter->list_entry.prio = task->prio;
	plist_add(&waiter->list_entry, &lock->wait_list);

	/* Release the task */
	raw_spin_unlock(&task->pi_lock);
	if (!rt_mutex_owner(lock)) {
		struct rt_mutex_waiter *lock_top_waiter;
		/*
		 * If the requeue above changed the top waiter, then we need
		 * to wake the new top waiter up to try to get the lock.
		 */
		lock_top_waiter = rt_mutex_top_waiter(lock);
		if (top_waiter != lock_top_waiter) {
			if (lock_top_waiter->savestate)
				wake_up_process_mutex(lock_top_waiter->task);
			else
				wake_up_process(lock_top_waiter->task);
		}
		raw_spin_unlock(&lock->wait_lock);
		goto out_put_task;
	}
	put_task_struct(task);

	/* Grab the next task */
	task = rt_mutex_owner(lock);
	get_task_struct(task);
	raw_spin_lock(&task->pi_lock);

	if (waiter == rt_mutex_top_waiter(lock)) {
		/* Boost the owner */
		plist_del(&top_waiter->pi_list_entry, &task->pi_waiters);
		waiter->pi_list_entry.prio = waiter->list_entry.prio;
		plist_add(&waiter->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);

	} else if (top_waiter == waiter) {
		/* Deboost the owner */
		plist_del(&waiter->pi_list_entry, &task->pi_waiters);
		waiter = rt_mutex_top_waiter(lock);
		waiter->pi_list_entry.prio = waiter->list_entry.prio;
		plist_add(&waiter->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);
	}

	raw_spin_unlock(&task->pi_lock);

	top_waiter = rt_mutex_top_waiter(lock);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	if (!detect_deadlock && waiter != top_waiter)
		goto out_put_task;

	goto again;

 out_unlock_pi:
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
 out_put_task:
	put_task_struct(task);

	return ret;
}

/*
 * Try to take an rt-mutex
 *
 * Must be called with lock->wait_lock held.
 *
 * @lock:   the lock to be acquired.
 * @task:   the task which wants to acquire the lock
 * @waiter: the waiter that is queued to the lock's wait list. (could be NULL)
 */
static int do_try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
				   struct rt_mutex_waiter *waiter, int mode)
{
	/*
	 * We have to be careful here if the atomic speedups are
	 * enabled, such that, when
	 *  - no other waiter is on the lock
	 *  - the lock has been released since we did the cmpxchg
	 * the lock can be released or taken while we are doing the
	 * checks and marking the lock with RT_MUTEX_HAS_WAITERS.
	 *
	 * The atomic acquire/release aware variant of
	 * mark_rt_mutex_waiters uses a cmpxchg loop. After setting
	 * the WAITERS bit, the atomic release / acquire can not
	 * happen anymore and lock->wait_lock protects us from the
	 * non-atomic case.
	 *
	 * Note, that this might set lock->owner =
	 * RT_MUTEX_HAS_WAITERS in the case the lock is not contended
	 * any more. This is fixed up when we take the ownership.
	 * This is the transitional state explained at the top of this file.
	 */
	mark_rt_mutex_waiters(lock);

	if (rt_mutex_owner(lock))
		return 0;

	/*
	 * It will get the lock because of one of these conditions:
	 * 1) there is no waiter
	 * 2) higher priority than waiters
	 * 3) it is top waiter
	 */
	if (rt_mutex_has_waiters(lock)) {
		struct task_struct *pendowner = rt_mutex_top_waiter(lock)->task;
		if (task != pendowner && !lock_is_stealable(task, pendowner, mode))
			return 0;
	}

	/* We got the lock. */

	if (waiter || rt_mutex_has_waiters(lock)) {
		unsigned long flags;
		struct rt_mutex_waiter *top;

		raw_spin_lock_irqsave(&task->pi_lock, flags);

		/* remove the queued waiter. */
		if (waiter) {
			plist_del(&waiter->list_entry, &lock->wait_list);
			task->pi_blocked_on = NULL;
		}

		/*
		 * We have to enqueue the top waiter(if it exists) into
		 * task->pi_waiters list.
		 */
		if (rt_mutex_has_waiters(lock)) {
			top = rt_mutex_top_waiter(lock);
			top->pi_list_entry.prio = top->list_entry.prio;
			plist_add(&top->pi_list_entry, &task->pi_waiters);
		}
		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	}

	debug_rt_mutex_lock(lock);

	rt_mutex_set_owner(lock, task);

	rt_mutex_deadlock_account_lock(lock, task);

	return 1;
}

static inline int
try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
		     struct rt_mutex_waiter *waiter)
{
	return do_try_to_take_rt_mutex(lock, task, waiter, STEAL_NORMAL);
}

/*
 * Task blocks on lock.
 *
 * Prepare waiter and propagate pi chain
 *
 * This must be called with lock->wait_lock held.
 */
static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
				   struct rt_mutex_waiter *waiter,
				   struct task_struct *task,
				   int detect_deadlock, unsigned long flags,
				   int savestate)
{
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *top_waiter = waiter;
	int chain_walk = 0, res;

	raw_spin_lock(&task->pi_lock);

	/*
	 * In the case of futex requeue PI, this will be a proxy
	 * lock. The task will wake unaware that it is enqueueed on
	 * this lock. Avoid blocking on two locks and corrupting
	 * pi_blocked_on via the PI_WAKEUP_INPROGRESS
	 * flag. futex_wait_requeue_pi() sets this when it wakes up
	 * before requeue (due to a signal or timeout). Do not enqueue
	 * the task if PI_WAKEUP_INPROGRESS is set.
	 */
	if (task != current && task->pi_blocked_on == PI_WAKEUP_INPROGRESS) {
		raw_spin_unlock(&task->pi_lock);
		return -EAGAIN;
	}

	BUG_ON(rt_mutex_real_waiter(task->pi_blocked_on));

	__rt_mutex_adjust_prio(task);
	waiter->task = task;
	waiter->lock = lock;
	waiter->savestate = savestate;
	plist_node_init(&waiter->list_entry, task->prio);
	plist_node_init(&waiter->pi_list_entry, task->prio);

	/* Get the top priority waiter on the lock */
	if (rt_mutex_has_waiters(lock))
		top_waiter = rt_mutex_top_waiter(lock);
	plist_add(&waiter->list_entry, &lock->wait_list);

	task->pi_blocked_on = waiter;

	raw_spin_unlock(&task->pi_lock);

	if (!owner)
		return 0;

	if (waiter == rt_mutex_top_waiter(lock)) {
		raw_spin_lock(&owner->pi_lock);
		plist_del(&top_waiter->pi_list_entry, &owner->pi_waiters);
		plist_add(&waiter->pi_list_entry, &owner->pi_waiters);

		__rt_mutex_adjust_prio(owner);
		if (rt_mutex_real_waiter(owner->pi_blocked_on))
			chain_walk = 1;
		raw_spin_unlock(&owner->pi_lock);
	}
	else if (debug_rt_mutex_detect_deadlock(waiter, detect_deadlock))
		chain_walk = 1;

	if (!chain_walk)
		return 0;

	/*
	 * The owner can't disappear while holding a lock,
	 * so the owner struct is protected by wait_lock.
	 * Gets dropped in rt_mutex_adjust_prio_chain()!
	 */
	get_task_struct(owner);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	res = rt_mutex_adjust_prio_chain(owner, detect_deadlock, lock, waiter,
					 task);

	raw_spin_lock_irq(&lock->wait_lock);

	return res;
}

/*
 * Wake up the next waiter on the lock.
 *
 * Remove the top waiter from the current tasks waiter list and wake it up.
 *
 * Called with lock->wait_lock held.
 */
static void wakeup_next_waiter(struct rt_mutex *lock, int savestate)
{
	struct rt_mutex_waiter *waiter;
	struct task_struct *top_task;

	waiter = rt_mutex_top_waiter(lock);
	top_task = waiter->task;

	/*
	 * Remove it from current->pi_waiters. We do not adjust a
	 * possible priority boost right now. We execute wakeup in the
	 * boosted mode and go back to normal after releasing
	 * lock->wait_lock.
	 */
	raw_spin_lock(&current->pi_lock);
	plist_del(&waiter->pi_list_entry, &current->pi_waiters);
	raw_spin_unlock(&current->pi_lock);

	rt_mutex_set_owner(lock, NULL);

	if (!savestate)
		wake_up_process(top_task);
	else
		wake_up_process_mutex(top_task);

	WARN_ON(!top_task->pi_blocked_on);
	WARN_ON(top_task->pi_blocked_on != waiter);
	WARN_ON(top_task->pi_blocked_on->lock != lock);
}

/*
 * Remove a waiter from a lock
 *
 * Must be called with lock->wait_lock held
 */
static void remove_waiter(struct rt_mutex *lock,
			  struct rt_mutex_waiter *waiter,
			  unsigned long flags)
{
	int first = (waiter == rt_mutex_top_waiter(lock));
	struct task_struct *owner = rt_mutex_owner(lock);
	int chain_walk = 0;

	raw_spin_lock(&current->pi_lock);
	plist_del(&waiter->list_entry, &lock->wait_list);
	current->pi_blocked_on = NULL;
	raw_spin_unlock(&current->pi_lock);

	if (!owner) {
		BUG_ON(first);
		return;
	}

	if (first) {

		raw_spin_lock(&owner->pi_lock);

		plist_del(&waiter->pi_list_entry, &owner->pi_waiters);

		if (rt_mutex_has_waiters(lock)) {
			struct rt_mutex_waiter *next;

			next = rt_mutex_top_waiter(lock);
			plist_add(&next->pi_list_entry, &owner->pi_waiters);
		}
		__rt_mutex_adjust_prio(owner);

		if (rt_mutex_real_waiter(owner->pi_blocked_on))
			chain_walk = 1;

		raw_spin_unlock(&owner->pi_lock);
	}

	WARN_ON(!plist_node_empty(&waiter->pi_list_entry));

	if (!chain_walk)
		return;

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(owner);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	rt_mutex_adjust_prio_chain(owner, 0, lock, NULL, current);

	raw_spin_lock_irq(&lock->wait_lock);
}

/*
 * Recheck the pi chain, in case we got a priority setting
 *
 * Called from sched_setscheduler
 */
void rt_mutex_adjust_pi(struct task_struct *task)
{
	struct rt_mutex_waiter *waiter;
	unsigned long flags;

	raw_spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	if (!rt_mutex_real_waiter(waiter) ||
	    waiter->list_entry.prio == task->prio) {
		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
		return;
	}

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(task);
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	rt_mutex_adjust_prio_chain(task, 0, NULL, NULL, task);
}

/*
 * preemptible spin_lock functions:
 */

#ifdef CONFIG_PREEMPT_RT

static inline void
rt_spin_lock_fastlock(struct rt_mutex *lock,
		void  (*slowfn)(struct rt_mutex *lock))
{
	/* Temporary HACK! */
	if (likely(!current->in_printk))
		might_sleep();
	else if (in_atomic() || irqs_disabled())
		/* don't grab locks for printk in atomic */
		return;

	if (likely(rt_mutex_cmpxchg(lock, NULL, current)))
		rt_mutex_deadlock_account_lock(lock, current);
	else
		slowfn(lock);
}

static inline void
rt_spin_lock_fastunlock(struct rt_mutex *lock,
			void  (*slowfn)(struct rt_mutex *lock))
{
	/* Temporary HACK! */
	if (unlikely(rt_mutex_owner(lock) != current) && current->in_printk)
		/* don't grab locks for printk in atomic */
		return;

	if (likely(rt_mutex_cmpxchg(lock, current, NULL)))
		rt_mutex_deadlock_account_unlock(current);
	else
		slowfn(lock);
}


#ifdef CONFIG_SMP
static int adaptive_wait(struct rt_mutex_waiter *waiter,
			 struct task_struct *orig_owner)
{
	for (;;) {

		/* Owner changed? Then lets update the original */
		if (orig_owner != rt_mutex_owner(waiter->lock))
			return 0;

		/* Owner went to bed, so should we */
		if (!task_is_current(orig_owner))
			return 1;

		cpu_relax();
	}
}
#else
static int adaptive_wait(struct rt_mutex_waiter *waiter,
			 struct task_struct *orig_owner)
{
	return 1;
}
#endif

/*
 * The state setting needs to preserve the original state and needs to
 * take care of non rtmutex wakeups.
 *
 * Called with rtmutex->wait_lock held to serialize against rtmutex
 * wakeups().
 */
static inline unsigned long
rt_set_current_blocked_state(unsigned long saved_state)
{
	unsigned long state, block_state;

	/*
	 * If state is TASK_INTERRUPTIBLE, then we set the state for
	 * blocking to TASK_INTERRUPTIBLE as well, otherwise we would
	 * miss real wakeups via wake_up_interruptible(). If such a
	 * wakeup happens we see the running state and preserve it in
	 * saved_state. Now we can ignore further wakeups as we will
	 * return in state running from our "spin" sleep.
	 */
	if (saved_state == TASK_INTERRUPTIBLE ||
		saved_state == TASK_STOPPED)
		block_state = saved_state;
	else
		block_state = TASK_UNINTERRUPTIBLE;

	state = xchg(&current->state, block_state);
	/*
	 * Take care of non rtmutex wakeups. rtmutex wakeups
	 * or TASK_RUNNING_MUTEX to (UN)INTERRUPTIBLE.
	 */
	if (state == TASK_RUNNING)
		saved_state = TASK_RUNNING;

	return saved_state;
}

static inline void rt_restore_current_state(unsigned long saved_state)
{
	unsigned long state = xchg(&current->state, saved_state);

	if (state == TASK_RUNNING)
		current->state = TASK_RUNNING;
}

/*
 * Slow path lock function spin_lock style: this variant is very
 * careful not to miss any non-lock wakeups.
 *
 * The wakeup side uses wake_up_process_mutex, which, combined with
 * the xchg code of this function is a transparent sleep/wakeup
 * mechanism nested within any existing sleep/wakeup mechanism. This
 * enables the seemless use of arbitrary (blocking) spinlocks within
 * sleep/wakeup event loops.
 */
static void  noinline __sched
rt_spin_lock_slowlock(struct rt_mutex *lock)
{
	struct rt_mutex_waiter waiter;
	unsigned long saved_state, flags;
	/* orig_owner is only set if next_waiter is set */
	struct task_struct *uninitialized_var(orig_owner);
	int next_waiter;
	int saved_lock_depth;
	int ret;

	debug_rt_mutex_init_waiter(&waiter);
	waiter.task = NULL;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	init_lists(lock);

	if (do_try_to_take_rt_mutex(lock, current, NULL, STEAL_LATERAL)) {
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
		return;
	}

	BUG_ON(rt_mutex_owner(lock) == current);

	/*
	 * Here we save whatever state the task was in originally,
	 * we'll restore it at the end of the function and we'll take
	 * any intermediate wakeup into account as well, independently
	 * of the lock sleep/wakeup mechanism. When we get a real
	 * wakeup the task->state is TASK_RUNNING and we change
	 * saved_state accordingly. If we did not get a real wakeup
	 * then we return with the saved state. We need to be careful
	 * about original state TASK_INTERRUPTIBLE as well, as we
	 * could miss a wakeup_interruptible()
	 */
	saved_state = rt_set_current_blocked_state(current->state);

	/*
	 * Prevent schedule() to drop BKL, while waiting for
	 * the lock ! We restore lock_depth when we come back.
	 */
	saved_lock_depth = current->lock_depth;
	current->lock_depth = -1;

	ret = task_blocks_on_rt_mutex(lock, &waiter, current, 0, flags, 1);
	BUG_ON(ret);

	for (;;) {
		int sleep = 1;

		/* Try to acquire the lock again. */
		if (do_try_to_take_rt_mutex(lock, current, &waiter, STEAL_LATERAL))
			break;

		next_waiter = &waiter == rt_mutex_top_waiter(lock);
		if (next_waiter) {
			orig_owner = rt_mutex_owner(lock);
			if (orig_owner)
				get_task_struct(orig_owner);
		}
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

		debug_rt_mutex_print_deadlock(&waiter);

		if (next_waiter && orig_owner) {
			if (!adaptive_wait(&waiter, orig_owner))
				sleep = 0;
			put_task_struct(orig_owner);
		}
		if (sleep)
			schedule_rt_mutex(lock);

		raw_spin_lock_irqsave(&lock->wait_lock, flags);
		saved_state = rt_set_current_blocked_state(saved_state);
	}
	current->lock_depth = saved_lock_depth;

	rt_restore_current_state(saved_state);

	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up:
	 */
	fixup_rt_mutex_waiters(lock);

	BUG_ON(rt_mutex_has_waiters(lock) && &waiter == rt_mutex_top_waiter(lock));
	BUG_ON(!plist_node_empty(&waiter.list_entry));

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	debug_rt_mutex_free_waiter(&waiter);
}

/*
 * Slow path to release a rt_mutex spin_lock style
 */
static void  noinline __sched
rt_spin_lock_slowunlock(struct rt_mutex *lock)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	debug_rt_mutex_unlock(lock);

	rt_mutex_deadlock_account_unlock(current);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
		return;
	}

	wakeup_next_waiter(lock, 1);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Undo pi boosting.when necessary */
	rt_mutex_adjust_prio(current);
}

void __lockfunc rt_spin_lock(spinlock_t *lock)
{
	rt_spin_lock_fastlock(&lock->lock, rt_spin_lock_slowlock);
	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
}
EXPORT_SYMBOL(rt_spin_lock);

void __lockfunc __rt_spin_lock(struct rt_mutex *lock)
{
	rt_spin_lock_fastlock(lock, rt_spin_lock_slowlock);
}
EXPORT_SYMBOL(__rt_spin_lock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC

void __lockfunc rt_spin_lock_nested(spinlock_t *lock, int subclass)
{
	rt_spin_lock_fastlock(&lock->lock, rt_spin_lock_slowlock);
	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
}
EXPORT_SYMBOL(rt_spin_lock_nested);

#endif

void __lockfunc rt_spin_unlock(spinlock_t *lock)
{
	/* NOTE: we always pass in '1' for nested, for simplicity */
	spin_release(&lock->dep_map, 1, _RET_IP_);
	rt_spin_lock_fastunlock(&lock->lock, rt_spin_lock_slowunlock);
}
EXPORT_SYMBOL(rt_spin_unlock);

void __lockfunc __rt_spin_unlock(struct rt_mutex *lock)
{
	rt_spin_lock_fastunlock(lock, rt_spin_lock_slowunlock);
}
EXPORT_SYMBOL(__rt_spin_unlock);

/*
 * Wait for the lock to get unlocked: instead of polling for an unlock
 * (like raw spinlocks do), we lock and unlock, to force the kernel to
 * schedule if there's contention:
 */
void __lockfunc rt_spin_unlock_wait(spinlock_t *lock)
{
	spin_lock(lock);
	spin_unlock(lock);
}
EXPORT_SYMBOL(rt_spin_unlock_wait);

int __lockfunc rt_spin_trylock(spinlock_t *lock)
{
	int ret = rt_mutex_trylock(&lock->lock);

	if (ret)
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(rt_spin_trylock);

int __lockfunc rt_spin_trylock_irqsave(spinlock_t *lock, unsigned long *flags)
{
	int ret;

	*flags = 0;
	ret = rt_mutex_trylock(&lock->lock);
	if (ret)
		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL(rt_spin_trylock_irqsave);

int atomic_dec_and_spin_lock(atomic_t *atomic, spinlock_t *lock)
{
	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
	if (atomic_add_unless(atomic, -1, 1))
		return 0;
	rt_spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	rt_spin_unlock(lock);
	return 0;
}
EXPORT_SYMBOL(atomic_dec_and_spin_lock);

void
__rt_spin_lock_init(spinlock_t *lock, char *name, struct lock_class_key *key)
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
EXPORT_SYMBOL(__rt_spin_lock_init);

#endif

static inline int rt_release_bkl(struct rt_mutex *lock, unsigned long flags)
{
	int saved_lock_depth = current->lock_depth;

#ifdef CONFIG_LOCK_KERNEL
	current->lock_depth = -1;
	/*
	 * try_to_take_lock set the waiters, make sure it's
	 * still correct.
	 */
	fixup_rt_mutex_waiters(lock);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	mutex_unlock(&kernel_sem);

	raw_spin_lock_irq(&lock->wait_lock);
#endif
	return saved_lock_depth;
}

static inline void rt_reacquire_bkl(int saved_lock_depth)
{
#ifdef CONFIG_LOCK_KERNEL
	mutex_lock(&kernel_sem);
	current->lock_depth = saved_lock_depth;
#endif
}

/**
 * __rt_mutex_slowlock() - Perform the wait-wake-try-to-take loop
 * @lock:		 the rt_mutex to take
 * @state:		 the state the task should block in (TASK_INTERRUPTIBLE
 *			 or TASK_UNINTERRUPTIBLE)
 * @timeout:		 the pre-initialized and started timer, or NULL for none
 * @waiter:		 the pre-initialized rt_mutex_waiter
 *
 * lock->wait_lock must be held by the caller.
 */
static int __sched
__rt_mutex_slowlock(struct rt_mutex *lock, int state,
		    struct hrtimer_sleeper *timeout,
		    struct rt_mutex_waiter *waiter,
		    unsigned long flags)
{
	int ret = 0;

	for (;;) {
		/* Try to acquire the lock: */
		if (try_to_take_rt_mutex(lock, current, waiter))
			break;

		/*
		 * TASK_INTERRUPTIBLE checks for signals and
		 * timeout. Ignored otherwise.
		 */
		if (unlikely(state == TASK_INTERRUPTIBLE)) {
			/* Signal pending? */
			if (signal_pending(current))
				ret = -EINTR;
			if (timeout && !timeout->task)
				ret = -ETIMEDOUT;
			if (ret)
				break;
		}

		raw_spin_unlock_irq(&lock->wait_lock);

		debug_rt_mutex_print_deadlock(waiter);

		schedule_rt_mutex(lock);

		raw_spin_lock_irq(&lock->wait_lock);

		set_current_state(state);
	}

	return ret;
}

/*
 * Slow path lock function:
 */
static int __sched
rt_mutex_slowlock(struct rt_mutex *lock, int state,
		  struct hrtimer_sleeper *timeout,
		  int detect_deadlock)
{
	int ret = 0, saved_lock_depth = -1;
	struct rt_mutex_waiter waiter;
	unsigned long flags;

	debug_rt_mutex_init_waiter(&waiter);

	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	init_lists(lock);

	/* Try to acquire the lock again: */
	if (try_to_take_rt_mutex(lock, current, NULL)) {
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
		return 0;
	}

	set_current_state(state);

	/* Setup the timer, when timeout != NULL */
	if (unlikely(timeout)) {
		hrtimer_start_expires(&timeout->timer, HRTIMER_MODE_ABS);
		if (!hrtimer_active(&timeout->timer))
			timeout->task = NULL;
	}

	ret = task_blocks_on_rt_mutex(lock, &waiter, current, detect_deadlock, flags, 0);

	/*
	 * We drop the BKL here before we go into the wait loop to avoid a
	 * possible deadlock in the scheduler.
	 *
	 * Note: This must be done after we call task_blocks_on_rt_mutex
	 *  because rt_release_bkl() releases the wait_lock and will
	 *  cause a race between setting the mark waiters flag in
	 *  the owner field and adding this task to the wait list. Those
	 *  two must be done within the protection of the wait_lock.
	 */
	if (unlikely(current->lock_depth >= 0))
		saved_lock_depth = rt_release_bkl(lock, flags);

	if (likely(!ret))
		ret = __rt_mutex_slowlock(lock, state, timeout, &waiter, flags);

	set_current_state(TASK_RUNNING);

	if (unlikely(ret))
		remove_waiter(lock, &waiter, flags);
	BUG_ON(!plist_node_empty(&waiter.list_entry));

	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Remove pending timer: */
	if (unlikely(timeout))
		hrtimer_cancel(&timeout->timer);

	/* Must we reaquire the BKL? */
	if (unlikely(saved_lock_depth >= 0))
		rt_reacquire_bkl(saved_lock_depth);

	debug_rt_mutex_free_waiter(&waiter);

	return ret;
}

/*
 * Slow path try-lock function:
 */
static inline int
rt_mutex_slowtrylock(struct rt_mutex *lock)
{
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	if (likely(rt_mutex_owner(lock) != current)) {

		init_lists(lock);

		ret = try_to_take_rt_mutex(lock, current, NULL);
		/*
		 * try_to_take_rt_mutex() sets the lock waiters
		 * bit unconditionally. Clean this up.
		 */
		fixup_rt_mutex_waiters(lock);
	}

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	return ret;
}

/*
 * Slow path to release a rt-mutex:
 */
static void __sched
rt_mutex_slowunlock(struct rt_mutex *lock)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	debug_rt_mutex_unlock(lock);

	rt_mutex_deadlock_account_unlock(current);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
		return;
	}

	wakeup_next_waiter(lock, 0);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Undo pi boosting if necessary: */
	rt_mutex_adjust_prio(current);
}

/*
 * debug aware fast / slowpath lock,trylock,unlock
 *
 * The atomic acquire/release ops are compiled away, when either the
 * architecture does not support cmpxchg or when debugging is enabled.
 */
static inline int
rt_mutex_fastlock(struct rt_mutex *lock, int state,
		  int detect_deadlock,
		  int (*slowfn)(struct rt_mutex *lock, int state,
				struct hrtimer_sleeper *timeout,
				int detect_deadlock))
{
	if (!detect_deadlock && likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 0;
	} else
		return slowfn(lock, state, NULL, detect_deadlock);
}

static inline int
rt_mutex_timed_fastlock(struct rt_mutex *lock, int state,
			struct hrtimer_sleeper *timeout, int detect_deadlock,
			int (*slowfn)(struct rt_mutex *lock, int state,
				      struct hrtimer_sleeper *timeout,
				      int detect_deadlock))
{
	if (!detect_deadlock && likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 0;
	} else
		return slowfn(lock, state, timeout, detect_deadlock);
}

static inline int
rt_mutex_fasttrylock(struct rt_mutex *lock,
		     int (*slowfn)(struct rt_mutex *lock))
{
	if (likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 1;
	}
	return slowfn(lock);
}

static inline void
rt_mutex_fastunlock(struct rt_mutex *lock,
		    void (*slowfn)(struct rt_mutex *lock))
{
	if (likely(rt_mutex_cmpxchg(lock, current, NULL)))
		rt_mutex_deadlock_account_unlock(current);
	else
		slowfn(lock);
}

/**
 * rt_mutex_lock_killable - lock a rt_mutex killable
 *
 * @lock: 		the rt_mutex to be locked
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int __sched rt_mutex_lock_killable(struct rt_mutex *lock,
				   int detect_deadlock)
{
	might_sleep();

	return rt_mutex_fastlock(lock, TASK_KILLABLE,
				 detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_killable);

/**
 * rt_mutex_lock - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 */
void __sched rt_mutex_lock(struct rt_mutex *lock)
{
	might_sleep();

	rt_mutex_fastlock(lock, TASK_UNINTERRUPTIBLE, 0, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock);

/**
 * rt_mutex_lock_interruptible - lock a rt_mutex interruptible
 *
 * @lock: 		the rt_mutex to be locked
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int __sched rt_mutex_lock_interruptible(struct rt_mutex *lock,
						 int detect_deadlock)
{
	might_sleep();

	return rt_mutex_fastlock(lock, TASK_INTERRUPTIBLE,
				 detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_interruptible);

/**
 * rt_mutex_timed_lock - lock a rt_mutex interruptible
 *			the timeout structure is provided
 *			by the caller
 *
 * @lock: 		the rt_mutex to be locked
 * @timeout:		timeout structure or NULL (no timeout)
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -ETIMEDOUT	when the timeout expired
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int
rt_mutex_timed_lock(struct rt_mutex *lock, struct hrtimer_sleeper *timeout,
		    int detect_deadlock)
{
	might_sleep();

	return rt_mutex_timed_fastlock(lock, TASK_INTERRUPTIBLE, timeout,
				       detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_timed_lock);

/**
 * rt_mutex_trylock - try to lock a rt_mutex
 *
 * @lock:	the rt_mutex to be locked
 *
 * Returns 1 on success and 0 on contention
 */
int __sched rt_mutex_trylock(struct rt_mutex *lock)
{
	return rt_mutex_fasttrylock(lock, rt_mutex_slowtrylock);
}
EXPORT_SYMBOL_GPL(rt_mutex_trylock);

/**
 * rt_mutex_unlock - unlock a rt_mutex
 *
 * @lock: the rt_mutex to be unlocked
 */
void __sched rt_mutex_unlock(struct rt_mutex *lock)
{
	rt_mutex_fastunlock(lock, rt_mutex_slowunlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_unlock);

/**
 * rt_mutex_destroy - mark a mutex unusable
 * @lock: the mutex to be destroyed
 *
 * This function marks the mutex uninitialized, and any subsequent
 * use of the mutex is forbidden. The mutex must not be locked when
 * this function is called.
 */
void rt_mutex_destroy(struct rt_mutex *lock)
{
	WARN_ON(rt_mutex_is_locked(lock));
#ifdef CONFIG_DEBUG_RT_MUTEXES
	lock->magic = NULL;
#endif
}

EXPORT_SYMBOL_GPL(rt_mutex_destroy);

/**
 * __rt_mutex_init - initialize the rt lock
 *
 * @lock: the rt lock to be initialized
 *
 * Initialize the rt lock to unlocked state.
 *
 * Initializing of a locked rt lock is not allowed
 */
void __rt_mutex_init(struct rt_mutex *lock, const char *name)
{
	lock->owner = NULL;
	raw_spin_lock_init(&lock->wait_lock);
	plist_head_init_raw(&lock->wait_list, &lock->wait_lock);

	debug_rt_mutex_init(lock, name);
}
EXPORT_SYMBOL_GPL(__rt_mutex_init);

/**
 * rt_mutex_init_proxy_locked - initialize and lock a rt_mutex on behalf of a
 *				proxy owner
 *
 * @lock: 	the rt_mutex to be locked
 * @proxy_owner:the task to set as owner
 *
 * No locking. Caller has to do serializing itself
 * Special API call for PI-futex support
 */
void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
				struct task_struct *proxy_owner)
{
	__rt_mutex_init(lock, NULL);
	debug_rt_mutex_proxy_lock(lock, proxy_owner);
	rt_mutex_set_owner(lock, proxy_owner);
	rt_mutex_deadlock_account_lock(lock, proxy_owner);
}

/**
 * rt_mutex_proxy_unlock - release a lock on behalf of owner
 *
 * @lock: 	the rt_mutex to be locked
 *
 * No locking. Caller has to do serializing itself
 * Special API call for PI-futex support
 */
void rt_mutex_proxy_unlock(struct rt_mutex *lock,
			   struct task_struct *proxy_owner)
{
	debug_rt_mutex_proxy_unlock(lock);
	rt_mutex_set_owner(lock, NULL);
	rt_mutex_deadlock_account_unlock(proxy_owner);
}

/**
 * rt_mutex_start_proxy_lock() - Start lock acquisition for another task
 * @lock:		the rt_mutex to take
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @task:		the task to prepare
 * @detect_deadlock:	perform deadlock detection (1) or not (0)
 *
 * Returns:
 *  0 - task blocked on lock
 *  1 - acquired the lock for task, caller should wake it up
 * <0 - error
 *
 * Special API call for FUTEX_REQUEUE_PI support.
 */
int rt_mutex_start_proxy_lock(struct rt_mutex *lock,
			      struct rt_mutex_waiter *waiter,
			      struct task_struct *task, int detect_deadlock)
{
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	if (try_to_take_rt_mutex(lock, task, NULL)) {
		raw_spin_unlock(&lock->wait_lock);
		return 1;
	}

	ret = task_blocks_on_rt_mutex(lock, waiter, task, detect_deadlock,
				      flags, 0);

	if (ret == -EDEADLK && !rt_mutex_owner(lock)) {
		/*
		 * Reset the return value. We might have
		 * returned with -EDEADLK and the owner
		 * released the lock while we were walking the
		 * pi chain.  Let the waiter sort it out.
		 */
		ret = 0;
	}

	if (unlikely(ret))
		remove_waiter(lock, waiter, flags);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	debug_rt_mutex_print_deadlock(waiter);

	return ret;
}

/**
 * rt_mutex_next_owner - return the next owner of the lock
 *
 * @lock: the rt lock query
 *
 * Returns the next owner of the lock or NULL
 *
 * Caller has to serialize against other accessors to the lock
 * itself.
 *
 * Special API call for PI-futex support
 */
struct task_struct *rt_mutex_next_owner(struct rt_mutex *lock)
{
	if (!rt_mutex_has_waiters(lock))
		return NULL;

	return rt_mutex_top_waiter(lock)->task;
}

/**
 * rt_mutex_finish_proxy_lock() - Complete lock acquisition
 * @lock:		the rt_mutex we were woken on
 * @to:			the timeout, null if none. hrtimer should already have
 * 			been started.
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @detect_deadlock:	perform deadlock detection (1) or not (0)
 *
 * Complete the lock acquisition started our behalf by another thread.
 *
 * Returns:
 *  0 - success
 * <0 - error, one of -EINTR, -ETIMEDOUT, or -EDEADLK
 *
 * Special API call for PI-futex requeue support
 */
int rt_mutex_finish_proxy_lock(struct rt_mutex *lock,
			       struct hrtimer_sleeper *to,
			       struct rt_mutex_waiter *waiter,
			       int detect_deadlock)
{
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	set_current_state(TASK_INTERRUPTIBLE);

	ret = __rt_mutex_slowlock(lock, TASK_INTERRUPTIBLE, to, waiter, flags);

	set_current_state(TASK_RUNNING);

	if (unlikely(ret))
		remove_waiter(lock, waiter, flags);

	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally. We might
	 * have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	return ret;
}
