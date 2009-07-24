#ifndef __LINUX_SPINLOCK_H
#define __LINUX_SPINLOCK_H

/*
 * include/linux/spinlock.h - generic spinlock/rwlock declarations
 *
 * here's the role of the various spinlock/rwlock related include files:
 *
 * on SMP builds:
 *
 *  asm/spinlock_types.h: contains the raw_spinlock_t/raw_rwlock_t and the
 *                        initializers
 *
 *  linux/spinlock_types.h:
 *                        defines the generic type and initializers
 *
 *  asm/spinlock.h:       contains the __raw_spin_*()/etc. lowlevel
 *                        implementations, mostly inline assembly code
 *
 *   (also included on UP-debug builds:)
 *
 *  linux/spinlock_api_smp.h:
 *                        contains the prototypes for the _spin_*() APIs.
 *
 *  linux/spinlock.h:     builds the final spin_*() APIs.
 *
 * on UP builds:
 *
 *  linux/spinlock_type_up.h:
 *                        contains the generic, simplified UP spinlock type.
 *                        (which is an empty structure on non-debug builds)
 *
 *  linux/spinlock_types.h:
 *                        defines the generic type and initializers
 *
 *  linux/spinlock_up.h:
 *                        contains the __raw_spin_*()/etc. version of UP
 *                        builds. (which are NOPs on non-debug, non-preempt
 *                        builds)
 *
 *   (included on UP-non-debug builds:)
 *
 *  linux/spinlock_api_up.h:
 *                        builds the _spin_*() APIs.
 *
 *  linux/spinlock.h:     builds the final spin_*() APIs.
 */

#include <linux/typecheck.h>
#include <linux/preempt.h>
#include <linux/linkage.h>
#include <linux/compiler.h>
#include <linux/thread_info.h>
#include <linux/kernel.h>
#include <linux/stringify.h>
#include <linux/bottom_half.h>

#include <asm/system.h>

/*
 * Must define these before including other files, inline functions need them
 */
#define LOCK_SECTION_NAME ".text.lock."KBUILD_BASENAME

#define LOCK_SECTION_START(extra)               \
        ".subsection 1\n\t"                     \
        extra                                   \
        ".ifndef " LOCK_SECTION_NAME "\n\t"     \
        LOCK_SECTION_NAME ":\n\t"               \
        ".endif\n"

#define LOCK_SECTION_END                        \
        ".previous\n\t"

#define __lockfunc __attribute__((section(".spinlock.text")))

/*
 * Pull the raw_spinlock_t and raw_rwlock_t definitions:
 */
#include <linux/spinlock_types.h>

extern int __lockfunc generic__raw_read_trylock(raw_rwlock_t *lock);

/*
 * Pull the __raw*() functions/declarations (UP-nondebug doesnt need them):
 */
#ifdef CONFIG_SMP
# include <asm/spinlock.h>
#else
# include <linux/spinlock_up.h>
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
  extern void __atomic_spin_lock_init(atomic_spinlock_t *lock,
				      const char *name,
				      struct lock_class_key *key);

# define atomic_spin_lock_init(lock)				\
do {								\
	static struct lock_class_key __key;			\
								\
	__atomic_spin_lock_init((lock), #lock, &__key);		\
} while (0)

#else
# define atomic_spin_lock_init(lock)				\
	do { *(lock) = __ATOMIC_SPIN_LOCK_UNLOCKED(lock); } while (0)
#endif

#define atomic_spin_is_locked(lock)	__raw_spin_is_locked(&(lock)->raw_lock)

#ifdef CONFIG_GENERIC_LOCKBREAK
#define atomic_spin_is_contended(lock) ((lock)->break_lock)
#else

#ifdef __raw_spin_is_contended
#define atomic_spin_is_contended(lock)	__raw_spin_is_contended(&(lock)->raw_lock)
#else
#define atomic_spin_is_contended(lock)	(((void)(lock), 0))
#endif /*__raw_spin_is_contended*/
#endif

/* The lock does not imply full memory barrier. */
#ifndef ARCH_HAS_SMP_MB_AFTER_LOCK
static inline void smp_mb__after_lock(void) { smp_mb(); }
#endif

/**
 * spin_unlock_wait - wait until the spinlock gets unlocked
 * @lock: the spinlock in question.
 */
#define atomic_spin_unlock_wait(lock)	__raw_spin_unlock_wait(&(lock)->raw_lock)

/*
 * Pull the _spin_*()/_read_*()/_write_*() functions/declarations:
 */
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
# include <linux/spinlock_api_smp.h>
#else
# include <linux/spinlock_api_up.h>
#endif

#ifdef CONFIG_DEBUG_SPINLOCK
 extern void _raw_spin_lock(atomic_spinlock_t *lock);
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)
 extern int _raw_spin_trylock(atomic_spinlock_t *lock);
 extern void _raw_spin_unlock(atomic_spinlock_t *lock);
#else
# define _raw_spin_lock(lock)		__raw_spin_lock(&(lock)->raw_lock)
# define _raw_spin_lock_flags(lock, flags) \
		__raw_spin_lock_flags(&(lock)->raw_lock, *(flags))
# define _raw_spin_trylock(lock)	__raw_spin_trylock(&(lock)->raw_lock)
# define _raw_spin_unlock(lock)		__raw_spin_unlock(&(lock)->raw_lock)
#endif

/*
 * Define the various spin_lock methods.  Note we define these
 * regardless of whether CONFIG_SMP or CONFIG_PREEMPT are set. The
 * various methods are defined as nops in the case they are not
 * required.
 */
#define atomic_spin_trylock(lock)	__cond_lock(lock, _atomic_spin_trylock(lock))

#define atomic_spin_lock(lock)		_atomic_spin_lock(lock)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define atomic_spin_lock_nested(lock, subclass) \
	_atomic_spin_lock_nested(lock, subclass)

# define atomic_spin_lock_nest_lock(lock, nest_lock)			\
	 do {								\
		 typecheck(struct lockdep_map *, &(nest_lock)->dep_map);\
		 _atomic_spin_lock_nest_lock(lock, &(nest_lock)->dep_map);\
	 } while (0)
#else
# define atomic_spin_lock_nested(lock, subclass) _atomic_spin_lock(lock)
# define atomic_spin_lock_nest_lock(lock, nest_lock) _atomic_spin_lock(lock)
#endif

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)

#define atomic_spin_lock_irqsave(lock, flags)		\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = _atomic_spin_lock_irqsave(lock);\
	} while (0)

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define atomic_spin_lock_irqsave_nested(lock, flags, subclass)		\
	do {								\
		typecheck(unsigned long, flags);			\
		flags = _atomic_spin_lock_irqsave_nested(lock, subclass);\
	} while (0)
#else
#define atomic_spin_lock_irqsave_nested(lock, flags, subclass)		\
	do {								\
		typecheck(unsigned long, flags);			\
		flags = _atomic_spin_lock_irqsave(lock);		\
	} while (0)
#endif

#else

#define atomic_spin_lock_irqsave(lock, flags)		\
	do {						\
		typecheck(unsigned long, flags);	\
		_atomic_spin_lock_irqsave(lock, flags);	\
	} while (0)

#define atomic_spin_lock_irqsave_nested(lock, flags, subclass)	\
	atomic_spin_lock_irqsave(lock, flags)

#endif

#define atomic_spin_lock_irq(lock)	_atomic_spin_lock_irq(lock)
#define atomic_spin_lock_bh(lock)	_atomic_spin_lock_bh(lock)

/*
 * We inline the unlock functions in the nondebug case:
 */
#if defined(CONFIG_DEBUG_SPINLOCK) || defined(CONFIG_PREEMPT) || \
	!defined(CONFIG_SMP)
# define atomic_spin_unlock(lock)	_atomic_spin_unlock(lock)
# define atomic_spin_unlock_irq(lock)	_atomic_spin_unlock_irq(lock)
#else
# define atomic_spin_unlock(lock) \
    do {__raw_spin_unlock(&(lock)->raw_lock); __release(lock); } while (0)

# define atomic_spin_unlock_irq(lock)		\
do {						\
	__raw_spin_unlock(&(lock)->raw_lock);	\
	__release(lock);			\
	local_irq_enable();			\
} while (0)
#endif

#define atomic_spin_unlock_irqrestore(lock, flags)	\
	do {						\
		typecheck(unsigned long, flags);	\
		_atomic_spin_unlock_irqrestore(lock, flags);\
	} while (0)
#define atomic_spin_unlock_bh(lock)	_atomic_spin_unlock_bh(lock)

#define atomic_spin_trylock_bh(lock)	\
	__cond_lock(lock, _atomic_spin_trylock_bh(lock))

#define atomic_spin_trylock_irq(lock) \
({ \
	local_irq_disable(); \
	atomic_spin_trylock(lock) ? \
	1 : ({ local_irq_enable(); 0;  }); \
})

#define atomic_spin_trylock_irqsave(lock, flags) \
({ \
	local_irq_save(flags); \
	atomic_spin_trylock(lock) ? \
	1 : ({ local_irq_restore(flags); 0; }); \
})

/**
 * spin_can_lock - would spin_trylock() succeed?
 * @lock: the spinlock in question.
 */
#define atomic_spin_can_lock(lock)	(!atomic_spin_is_locked(lock))

/*
 * Pull the atomic_t declaration:
 * (asm-mips/atomic.h needs above definitions)
 */
#include <asm/atomic.h>
/**
 * atomic_dec_and_lock - lock on reaching reference count zero
 * @atomic: the atomic counter
 * @lock: the spinlock in question
 *
 * Decrements @atomic by 1.  If the result is 0, returns true and locks
 * @lock.  Returns false for all other cases.
 */
extern int
_atomic_dec_and_atomic_lock(atomic_t *atomic, atomic_spinlock_t *lock);

#define atomic_dec_and_atomic_lock(atomic, lock) \
	__cond_lock(lock, _atomic_dec_and_atomic_lock(atomic, lock))

/*
 * Map spin* to atomic_spin* for PREEMPT_RT=n
 */
static inline void spin_lockcheck(spinlock_t *lock) { }

#define spin_lock_init(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_init((atomic_spinlock_t *)lock); \
} while (0)

#define spin_lock(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock((atomic_spinlock_t *)lock); \
} while (0)

#define spin_lock_bh(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_bh((atomic_spinlock_t *)lock);	\
} while (0)

#define spin_trylock(lock) \
({ \
	spin_lockcheck(lock);			\
	atomic_spin_trylock((atomic_spinlock_t *)lock);	\
})

#define spin_lock_nested(lock, subclass) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_nested((atomic_spinlock_t *)lock, subclass); \
} while (0)

#define spin_lock_nest_lock(lock, nest_lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_nest_lock((atomic_spinlock_t *)lock, nest_lock); \
} while (0)

#define spin_lock_irq(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_irq((atomic_spinlock_t *)lock); \
} while (0)

#define spin_lock_irqsave(lock, flags) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_irqsave((atomic_spinlock_t *)lock, flags);	\
} while (0)

#define spin_lock_irqsave_nested(lock, flags, subclass) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_lock_irqsave_nested((atomic_spinlock_t *)lock, flags, subclass); \
} while (0)

#define spin_unlock(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_unlock((atomic_spinlock_t *)lock); \
} while (0)

#define spin_unlock_bh(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_unlock_bh((atomic_spinlock_t *)lock); \
} while (0)

#define spin_unlock_irq(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_unlock_irq((atomic_spinlock_t *)lock); \
} while (0)

#define spin_unlock_irqrestore(lock, flags) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_unlock_irqrestore((atomic_spinlock_t *)lock, flags); \
} while (0)

#define spin_trylock_bh(lock) \
({ \
	spin_lockcheck(lock); \
	atomic_spin_trylock_bh((atomic_spinlock_t *)lock); \
})

#define spin_trylock_irq(lock) \
({ \
	spin_lockcheck(lock); \
	atomic_spin_trylock_irq((atomic_spinlock_t *)lock); \
})

#define spin_trylock_irqsave(lock, flags) \
({				  \
	spin_lockcheck(lock); \
	atomic_spin_trylock_irqsave((atomic_spinlock_t *)lock, flags); \
})

#define spin_unlock_wait(lock) \
do { \
	spin_lockcheck(lock); \
	atomic_spin_unlock_wait((atomic_spinlock_t *)lock); \
} while (0)

#define spin_is_locked(lock) \
({ \
	spin_lockcheck(lock); \
	atomic_spin_is_locked((atomic_spinlock_t *)lock); \
})

#define spin_is_contended(lock)	\
({ \
	spin_lockcheck(lock); \
	atomic_spin_is_contended((atomic_spinlock_t *)lock); \
})

#define spin_can_lock(lock) \
({ \
	spin_lockcheck(lock); \
	atomic_spin_can_lock((atomic_spinlock_t *)lock); \
})

#define assert_spin_locked(lock) \
do { \
	spin_lockcheck(lock); \
	assert_atomic_spin_locked((atomic_spinlock_t *)lock); \
} while (0)

#define atomic_dec_and_lock(atomic, lock) \
({ \
	spin_lockcheck(lock); \
	atomic_dec_and_atomic_lock(atomic, (atomic_spinlock_t *)lock); \
})

/*
 * Get the rwlock part
 */
#include <linux/rwlock.h>

#endif /* __LINUX_SPINLOCK_H */
