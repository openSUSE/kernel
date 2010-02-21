#ifndef __LINUX_RT_LOCK_H
#define __LINUX_RT_LOCK_H

/*
 * Real-Time Preemption Support
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains the main data structure definitions.
 */
#include <linux/rtmutex.h>
#include <asm/atomic.h>
#include <linux/spinlock_types.h>

#ifdef CONFIG_PREEMPT_RT

static inline int preempt_rt(void) { return 1; }

/*
 * spinlocks - an RT mutex plus lock-break field:
 */
typedef struct spinlock {
	struct rt_mutex		lock;
	unsigned int		break_lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} spinlock_t;

#ifdef CONFIG_DEBUG_RT_MUTEXES
# define __RT_SPIN_INITIALIZER(name) \
	{ \
	.wait_lock = __RAW_SPIN_LOCK_UNLOCKED(name), \
	.save_state = 1, \
	.file = __FILE__, \
	.line = __LINE__ , \
	}
#else
# define __RT_SPIN_INITIALIZER(name) \
	{ .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(name) }
#endif

#define __SPIN_LOCK_UNLOCKED(name)			\
	{ .lock = __RT_SPIN_INITIALIZER(name),		\
	  SPIN_DEP_MAP_INIT(name) }

#define SPIN_LOCK_UNLOCKED	__SPIN_LOCK_UNLOCKED(spin_old_style)

#define __DEFINE_SPINLOCK(name) \
	spinlock_t name = __SPIN_LOCK_UNLOCKED(name)

#define DEFINE_SPINLOCK(name) \
	spinlock_t name __cacheline_aligned_in_smp = __SPIN_LOCK_UNLOCKED(name)

extern void
__rt_spin_lock_init(spinlock_t *lock, char *name, struct lock_class_key *key);

#define spin_lock_init(lock)				\
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_spin_lock_init(lock, #lock, &__key);	\
} while (0)

extern void __lockfunc rt_spin_lock(spinlock_t *lock);
extern void __lockfunc rt_spin_lock_nested(spinlock_t *lock, int subclass);
extern void __lockfunc rt_spin_unlock(spinlock_t *lock);
extern void __lockfunc rt_spin_unlock_wait(spinlock_t *lock);
extern int __lockfunc
rt_spin_trylock_irqsave(spinlock_t *lock, unsigned long *flags);
extern int __lockfunc rt_spin_trylock(spinlock_t *lock);
extern int atomic_dec_and_spin_lock(atomic_t *atomic, spinlock_t *lock);

/*
 * lockdep-less calls, for derived types like rwlock:
 * (for trylock they can use rt_mutex_trylock() directly.
 */
extern void __lockfunc __rt_spin_lock(struct rt_mutex *lock);
extern void __lockfunc __rt_spin_unlock(struct rt_mutex *lock);

/*
 * rwlocks - an RW semaphore plus lock-break field:
 */
typedef struct {
	struct rt_mutex		lock;
	int			read_depth;
	unsigned int		break_lock;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
} rwlock_t;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define RW_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
#else
# define RW_DEP_MAP_INIT(lockname)
#endif

#define __RW_LOCK_UNLOCKED(name) \
	{ .lock = __RT_SPIN_INITIALIZER(name),	\
	  RW_DEP_MAP_INIT(name) }

#define RW_LOCK_UNLOCKED	__RW_LOCK_UNLOCKED(rw_old_style)

#define DEFINE_RWLOCK(name) \
	rwlock_t name __cacheline_aligned_in_smp = __RW_LOCK_UNLOCKED(name)

extern void __lockfunc rt_write_lock(rwlock_t *rwlock);
extern void __lockfunc rt_read_lock(rwlock_t *rwlock);
extern int __lockfunc rt_write_trylock(rwlock_t *rwlock);
extern int __lockfunc rt_write_trylock_irqsave(rwlock_t *trylock,
					       unsigned long *flags);
extern int __lockfunc rt_read_trylock(rwlock_t *rwlock);
extern void __lockfunc rt_write_unlock(rwlock_t *rwlock);
extern void __lockfunc rt_read_unlock(rwlock_t *rwlock);
extern unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock);
extern unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock);
extern void
__rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key);

#define rwlock_init(rwl)				\
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_rwlock_init(rwl, #rwl, &__key);		\
} while (0)

/*
 * RW-semaphores are a spinlock plus a reader-depth count.
 *
 * Note that the semantics are different from the usual
 * Linux rw-sems, in PREEMPT_RT mode we do not allow
 * multiple readers to hold the lock at once, we only allow
 * a read-lock owner to read-lock recursively. This is
 * better for latency, makes the implementation inherently
 * fair and makes it simpler as well:
 */
struct rw_semaphore {
	struct rt_mutex		lock;
	int			read_depth;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

#define __RWSEM_INITIALIZER(name) \
	{ .lock = __RT_MUTEX_INITIALIZER(name.lock), \
	  RW_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(lockname) \
	struct rw_semaphore lockname = __RWSEM_INITIALIZER(lockname)

extern void  __rt_rwsem_init(struct rw_semaphore *rwsem, char *name,
				     struct lock_class_key *key);

# define rt_init_rwsem(sem)				\
do {							\
	static struct lock_class_key __key;		\
							\
	__rt_rwsem_init((sem), #sem, &__key);		\
} while (0)

extern void  rt_down_write(struct rw_semaphore *rwsem);
extern void
rt_down_read_nested(struct rw_semaphore *rwsem, int subclass);
extern void
rt_down_write_nested(struct rw_semaphore *rwsem, int subclass);
extern void  rt_down_read(struct rw_semaphore *rwsem);
extern int  rt_down_write_trylock(struct rw_semaphore *rwsem);
extern int  rt_down_read_trylock(struct rw_semaphore *rwsem);
extern void  rt_up_read(struct rw_semaphore *rwsem);
extern void  rt_up_write(struct rw_semaphore *rwsem);
extern void  rt_downgrade_write(struct rw_semaphore *rwsem);

#else

static inline int preempt_rt(void) { return 0; }

#endif /* CONFIG_PREEMPT_RT */

#endif

