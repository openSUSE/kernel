/*
 * Copyright (c) 2008 Intel Corporation
 * Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * Distributed under the terms of the GNU GPL, version 2
 *
 * Please see kernel/semaphore.c for documentation of these functions
 */
#ifndef __LINUX_SEMAPHORE_H
#define __LINUX_SEMAPHORE_H

#include <linux/list.h>
#include <linux/spinlock.h>

/* Please don't access any members of this structure directly */
struct anon_semaphore {
	spinlock_t		lock;
	unsigned int		count;
	struct list_head	wait_list;
};

#define __ANON_SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __SPIN_LOCK_UNLOCKED((name).lock),		\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define DEFINE_ANON_SEMAPHORE(name)	\
	struct anon_semaphore name = __ANON_SEMAPHORE_INITIALIZER(name, 1)

static inline void anon_sema_init(struct anon_semaphore *sem, int val)
{
	static struct lock_class_key __key;
	*sem = (struct anon_semaphore) __ANON_SEMAPHORE_INITIALIZER(*sem, val);
	lockdep_init_map(&sem->lock.dep_map, "semaphore->lock", &__key, 0);
}

static inline void anon_semaphore_init(struct anon_semaphore *sem)
{
	anon_sema_init(sem, 1);
}

/*
 * semaphore_init_locked() is mostly a sign for a mutex which is
 * abused as completion.
 */
static inline void __deprecated
anon_semaphore_init_locked(struct anon_semaphore *sem)
{
	anon_sema_init(sem, 0);
}

extern void anon_down(struct anon_semaphore *sem);
extern int __must_check anon_down_interruptible(struct anon_semaphore *sem);
extern int __must_check anon_down_killable(struct anon_semaphore *sem);
extern int __must_check anon_down_trylock(struct anon_semaphore *sem);
extern int __must_check anon_down_timeout(struct anon_semaphore *sem, long jiffies);
extern void anon_up(struct anon_semaphore *sem);

#ifdef CONFIG_PREEMPT_RT

static inline void sema_init(struct semaphore *sem, int val)
{
	rt_sema_init(sem, val);
}

static inline void semaphore_init(struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void down(struct semaphore *sem)
{
	rt_down(sem);
}

static inline int __must_check down_interruptible(struct semaphore *sem)
{
	return rt_down_interruptible(sem);
}

static inline int __must_check down_trylock(struct semaphore *sem)
{
	return rt_down_trylock(sem);
}

static inline int __must_check
down_timeout(struct semaphore *sem, long jiffies)
{
	return rt_down_timeout(sem, jiffies);
}

static inline void up(struct semaphore *sem)
{
	rt_up(sem);
}


#else
/*
 * Non preempt-rt maps semaphores to anon semaphores
 */
struct semaphore {
	spinlock_t		lock;
	unsigned int		count;
	struct list_head	wait_list;
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __SPIN_LOCK_UNLOCKED((name).lock),		\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define DEFINE_SEMAPHORE(name)	\
	struct semaphore name =  __SEMAPHORE_INITIALIZER(name, 1)

static inline void sema_init(struct semaphore *sem, int val)
{
	anon_sema_init((struct anon_semaphore *)sem, val);
}

static inline void semaphore_init(struct semaphore *sem)
{
	anon_sema_init((struct anon_semaphore *)sem, 1);
}

/*
 * semaphore_init_locked() is mostly a sign for a mutex which is
 * abused as completion.
 */
static inline void __deprecated semaphore_init_locked(struct semaphore *sem)
{
	anon_sema_init((struct anon_semaphore *)sem, 0);
}

static inline void down(struct semaphore *sem)
{
	anon_down((struct anon_semaphore *)sem);
}

static inline int __must_check down_interruptible(struct semaphore *sem)
{
	return anon_down_interruptible((struct anon_semaphore *)sem);
}
static inline int __must_check down_killable(struct semaphore *sem)
{
	return anon_down_killable((struct anon_semaphore *)sem);
}

static inline int __must_check down_trylock(struct semaphore *sem)
{
	return anon_down_trylock((struct anon_semaphore *)sem);
}

static inline int __must_check
down_timeout(struct semaphore *sem, long jiffies)
{
	return anon_down_timeout((struct anon_semaphore *)sem, jiffies);
}

static inline void up(struct semaphore *sem)
{
	anon_up((struct anon_semaphore *)sem);
}
#endif

#endif /* __LINUX_SEMAPHORE_H */
