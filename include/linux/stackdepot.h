/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * A generic stack depot implementation
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on code by Dmitry Chernenkov.
 */

#ifndef _LINUX_STACKDEPOT_H
#define _LINUX_STACKDEPOT_H

#include <linux/gfp.h>
#include <linux/refcount.h>

typedef u32 depot_stack_handle_t;

#define DEPOT_STACK_BITS (sizeof(depot_stack_handle_t) * 8)

#define STACK_ALLOC_NULL_PROTECTION_BITS 1
#define STACK_ALLOC_ORDER 2 /* 'Slab' size order for stack depot, 4 pages */
#define STACK_ALLOC_SIZE (1LL << (PAGE_SHIFT + STACK_ALLOC_ORDER))
#define STACK_ALLOC_ALIGN 4
#define STACK_ALLOC_OFFSET_BITS (STACK_ALLOC_ORDER + PAGE_SHIFT - \
				 STACK_ALLOC_ALIGN)
#define STACK_ALLOC_INDEX_BITS (DEPOT_STACK_BITS - \
		STACK_ALLOC_NULL_PROTECTION_BITS - STACK_ALLOC_OFFSET_BITS)

/* The compact structure to store the reference to stacks. */
union handle_parts {
	depot_stack_handle_t handle;
	struct {
		u32 slabindex : STACK_ALLOC_INDEX_BITS; /* slabindex is offset by 1 */
		u32 offset : STACK_ALLOC_OFFSET_BITS;
		u32 valid : STACK_ALLOC_NULL_PROTECTION_BITS;
	};
};

struct stack_record {
	struct stack_record *next;	/* Link in the hashtable */
	u32 hash;			/* Hash in the hastable */
	u32 size;			/* Number of frames in the stack */
	union handle_parts handle;
	refcount_t count;
	unsigned long entries[];	/* Variable-sized array of entries. */
};


/*
 * Every user of stack depot has to call this during its own init when it's
 * decided that it will be calling stack_depot_save() later.
 *
 * The alternative is to select STACKDEPOT_ALWAYS_INIT to have stack depot
 * enabled as part of mm_init(), for subsystems where it's known at compile time
 * that stack depot will be used.
 */
int stack_depot_init(void);

#ifdef CONFIG_STACKDEPOT_ALWAYS_INIT
static inline int stack_depot_early_init(void)	{ return stack_depot_init(); }
#else
static inline int stack_depot_early_init(void)	{ return 0; }
#endif

depot_stack_handle_t stack_depot_save(unsigned long *entries,
				      unsigned int nr_entries, gfp_t gfp_flags);

unsigned int stack_depot_fetch(depot_stack_handle_t handle,
			       unsigned long **entries);

unsigned int filter_irq_stacks(unsigned long *entries, unsigned int nr_entries);

int stack_depot_snprint(depot_stack_handle_t handle, char *buf, size_t size,
		       int spaces);

void stack_depot_print(depot_stack_handle_t stack);

#endif
