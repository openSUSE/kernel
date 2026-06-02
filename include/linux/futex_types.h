/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FUTEX_TYPES_H
#define _LINUX_FUTEX_TYPES_H

#ifdef CONFIG_FUTEX
#include <linux/compiler_types.h>
#include <linux/mutex_types.h>
#include <linux/types.h>

struct compat_robust_list_head;
struct futex_pi_state;
struct robust_list_head;

/**
 * struct futex_sched_data - Futex related per task data
 * @robust_list:	User space registered robust list pointer
 * @compat_robust_list:	User space registered robust list pointer for compat tasks
 * @pi_state_list:	List head for Priority Inheritance (PI) state management
 * @pi_state_cache:	Pointer to cache one PI state object per task
 * @exit_mutex:		Mutex for serializing exit
 * @state:		Futex handling state to handle exit races correctly
 */
struct futex_sched_data {
	struct robust_list_head __user		*robust_list;
#ifdef CONFIG_COMPAT
	struct compat_robust_list_head __user	*compat_robust_list;
#endif
	struct list_head			pi_state_list;
	struct futex_pi_state			*pi_state_cache;
	struct mutex				exit_mutex;
	unsigned int				state;
};

#ifdef CONFIG_FUTEX_PRIVATE_HASH
/**
 * struct futex_mm_phash - Futex private hash related per MM data
 * @lock:	Mutex to protect the private hash operations
 * @hash:	RCU managed pointer to the private hash
 * @hash_new:	Pointer to a newly allocated private hash
 * @batches:	Batch state for RCU synchronization
 * @rcu:	RCU head for call_rcu()
 * @atomic:	Aggregate value for @hash_ref
 * @ref:	Per CPU reference counter for a private hash
 */
struct futex_mm_phash {
	struct mutex			lock;
	struct futex_private_hash	__rcu *hash;
	struct futex_private_hash	*hash_new;
	unsigned long			batches;
	struct rcu_head			rcu;
	atomic_long_t			atomic;
	unsigned int			__percpu *ref;
};
#else  /* CONFIG_FUTEX_ROBUST_UNLOCK */
struct futex_mm_phash { };
#endif /* !CONFIG_FUTEX_ROBUST_UNLOCK */

/**
 * struct futex_mm_data - Futex related per MM data
 * @phash:	Futex private hash related data
 */
struct futex_mm_data {
	struct futex_mm_phash		phash;
};
#else  /* CONFIG_FUTEX */
struct futex_sched_data { };
struct futex_mm_data { };
#endif /* !CONFIG_FUTEX */

#endif /* _LINUX_FUTEX_TYPES_H */
