/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_JUMP_LABEL_TYPE_H
#define _LINUX_JUMP_LABEL_TYPE_H

#ifdef CONFIG_JUMP_LABEL

struct static_key {
	atomic_t enabled;
/*
 * Note:
 *   To make anonymous unions work with old compilers, the static
 *   initialization of them requires brackets. This creates a dependency
 *   on the order of the struct with the initializers. If any fields
 *   are added, STATIC_KEY_INIT_TRUE and STATIC_KEY_INIT_FALSE may need
 *   to be modified.
 *
 * bit 0 => 1 if key is initially true
 *	    0 if initially false
 * bit 1 => 1 if points to struct static_key_mod
 *	    0 if points to struct jump_entry
 */
	union {
		unsigned long type;
		struct jump_entry *entries;
		struct static_key_mod *next;
	};
};

#else
struct static_key {
	atomic_t enabled;
};
#endif	/* CONFIG_JUMP_LABEL */

struct static_key_true {
	struct static_key key;
};

struct static_key_false {
	struct static_key key;
};

#define DECLARE_STATIC_KEY_TRUE(name)	\
	extern struct static_key_true name

#define DECLARE_STATIC_KEY_FALSE(name)	\
	extern struct static_key_false name

#endif /* _LINUX_JUMP_LABEL_TYPE_H */
