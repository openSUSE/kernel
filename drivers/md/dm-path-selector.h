/*
 * Copyright (C) 2003 Sistina Software.
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * Module Author: Heinz Mauelshagen
 *
 * This file is released under the GPL.
 *
 * Path-Selector registration.
 */

#ifndef	DM_PATH_SELECTOR_H
#define	DM_PATH_SELECTOR_H

#include <linux/device-mapper.h>

#include "dm-mpath.h"

/*
 * We provide an abstraction for the code that chooses which path
 * to send some io down.
 */
struct path_selector_type;
struct path_selector {
	struct path_selector_type *type;
	void *context;
};

/*
 * Constructs a path selector object, takes custom arguments
 */
typedef int (*ps_ctr_fn) (struct path_selector *ps, unsigned argc, char **argv);
typedef void (*ps_dtr_fn) (struct path_selector *ps);

/*
 * Add an opaque path object, along with some selector specific
 * path args (eg, path priority).
 */
typedef	int (*ps_add_path_fn) (struct path_selector *ps, struct path *path,
			       int argc, char **argv, char **error);

/*
 * Chooses a path for this io, if no paths are available then
 * NULL will be returned.
 *
 * repeat_count is the number of times to use the path before
 * calling the function again.  0 means don't call it again unless
 * the path fails.
 */
typedef	struct path *(*ps_select_path_fn) (struct path_selector *ps,
					   unsigned *repeat_count);

/*
 * Notify the selector that a path has failed.
 */
typedef	void (*ps_fail_path_fn) (struct path_selector *ps,
				 struct path *p);

/*
 * Ask selector to reinstate a path.
 */
typedef	int (*ps_reinstate_path_fn) (struct path_selector *ps,
				     struct path *p);

/*
 * Table content based on parameters added in ps_add_path_fn
 * or path selector status
 */
typedef	int (*ps_status_fn) (struct path_selector *ps,
			     struct path *path,
			     status_type_t type,
			     char *result, unsigned int maxlen);

typedef int (*ps_end_io_fn) (struct path_selector *ps, struct path *path);

/* Information about a path selector type */
struct path_selector_type {
	char *name;
	struct module *module;

	unsigned int table_args;
	unsigned int info_args;
	ps_ctr_fn ctr;
	ps_dtr_fn dtr;

	ps_add_path_fn add_path;
	ps_fail_path_fn fail_path;
	ps_reinstate_path_fn reinstate_path;
	ps_select_path_fn select_path;
	ps_status_fn status;
	ps_end_io_fn end_io;
};

/* Register a path selector */
int dm_register_path_selector(struct path_selector_type *type);

/* Unregister a path selector */
int dm_unregister_path_selector(struct path_selector_type *type);

/* Returns a registered path selector type */
struct path_selector_type *dm_get_path_selector(const char *name);

/* Releases a path selector  */
void dm_put_path_selector(struct path_selector_type *pst);

#endif
