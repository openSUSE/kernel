/* rwsem.h: R/W semaphores, public interface
 *
 * Written by David Howells (dhowells@redhat.com).
 * Derived from asm-i386/semaphore.h
 */

#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <linux/linkage.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/rt_lock.h>
#include <asm/system.h>
#include <asm/atomic.h>

struct rw_anon_semaphore;
struct rw_semaphore;

#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
#include <linux/rwsem-spinlock.h> /* use a generic implementation */
#else
#include <asm/rwsem.h> /* use an arch-specific implementation */
#endif

/*
 * lock for reading
 */
extern void anon_down_read(struct rw_anon_semaphore *sem);

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
extern int anon_down_read_trylock(struct rw_anon_semaphore *sem);

/*
 * lock for writing
 */
extern void anon_down_write(struct rw_anon_semaphore *sem);

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
extern int anon_down_write_trylock(struct rw_anon_semaphore *sem);

/*
 * release a read lock
 */
extern void anon_up_read(struct rw_anon_semaphore *sem);

/*
 * release a write lock
 */
extern void anon_up_write(struct rw_anon_semaphore *sem);

/*
 * downgrade write lock to read lock
 */
extern void anon_downgrade_write(struct rw_anon_semaphore *sem);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
/*
 * nested locking. NOTE: rwsems are not allowed to recurse
 * (which occurs if the same task tries to acquire the same
 * lock instance multiple times), but multiple locks of the
 * same lock class might be taken, if the order of the locks
 * is always the same. This ordering rule can be expressed
 * to lockdep via the _nested() APIs, but enumerating the
 * subclasses that are used. (If the nesting relationship is
 * static then another method for expressing nested locking is
 * the explicit definition of lock class keys and the use of
 * lockdep_set_class() at lock initialization time.
 * See Documentation/lockdep-design.txt for more details.)
 */
extern void anon_down_read_nested(struct rw_anon_semaphore *sem, int subclass);
extern void anon_down_write_nested(struct rw_anon_semaphore *sem, int subclass);
/*
 * Take/release a lock when not the owner will release it.
 *
 * [ This API should be avoided as much as possible - the
 *   proper abstraction for this case is completions. ]
 */
extern void anon_down_read_non_owner(struct rw_anon_semaphore *sem);
extern void anon_up_read_non_owner(struct rw_anon_semaphore *sem);
#else
# define anon_down_read_nested(sem, subclass)	anon_down_read(sem)
# define anon_down_write_nested(sem, subclass)	anon_down_write(sem)
# define anon_down_read_non_owner(sem)		anon_down_read(sem)
# define anon_up_read_non_owner(sem)		anon_up_read(sem)
#endif

#ifdef CONFIG_PREEMPT_RT

#include <linux/rt_lock.h>

#define init_rwsem(sem)		rt_init_rwsem(sem)
#define rwsem_is_locked(s)	rt_mutex_is_locked(&(s)->lock)

static inline void down_read(struct rw_semaphore *sem)
{
	rt_down_read(sem);
}

static inline int down_read_trylock(struct rw_semaphore *sem)
{
	return rt_down_read_trylock(sem);
}

static inline void down_write(struct rw_semaphore *sem)
{
	rt_down_write(sem);
}

static inline int down_write_trylock(struct rw_semaphore *sem)
{
	return rt_down_write_trylock(sem);
}

static inline void up_read(struct rw_semaphore *sem)
{
	rt_up_read(sem);
}

static inline void up_write(struct rw_semaphore *sem)
{
	rt_up_write(sem);
}

static inline void downgrade_write(struct rw_semaphore *sem)
{
	rt_downgrade_write(sem);
}

static inline void down_read_nested(struct rw_semaphore *sem, int subclass)
{
	return rt_down_read_nested(sem, subclass);
}

static inline void down_write_nested(struct rw_semaphore *sem, int subclass)
{
	rt_down_write_nested(sem, subclass);
}

#else
/*
 * Non preempt-rt implementations
 */
static inline void down_read(struct rw_semaphore *sem)
{
	anon_down_read((struct rw_anon_semaphore *)sem);
}

static inline int down_read_trylock(struct rw_semaphore *sem)
{
	return anon_down_read_trylock((struct rw_anon_semaphore *)sem);
}

static inline void down_write(struct rw_semaphore *sem)
{
	anon_down_write((struct rw_anon_semaphore *)sem);
}

static inline int down_write_trylock(struct rw_semaphore *sem)
{
	return anon_down_write_trylock((struct rw_anon_semaphore *)sem);
}

static inline void up_read(struct rw_semaphore *sem)
{
	anon_up_read((struct rw_anon_semaphore *)sem);
}

static inline void up_write(struct rw_semaphore *sem)
{
	anon_up_write((struct rw_anon_semaphore *)sem);
}

static inline void downgrade_write(struct rw_semaphore *sem)
{
	anon_downgrade_write((struct rw_anon_semaphore *)sem);
}

static inline void down_read_nested(struct rw_semaphore *sem, int subclass)
{
	return anon_down_read_nested((struct rw_anon_semaphore *)sem, subclass);
}

static inline void down_write_nested(struct rw_semaphore *sem, int subclass)
{
	anon_down_write_nested((struct rw_anon_semaphore *)sem, subclass);
}
#endif

#endif /* _LINUX_RWSEM_H */
