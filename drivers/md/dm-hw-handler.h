/*
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 *
 * Multipath hardware handler registration.
 */

#ifndef	DM_HW_HANDLER_H
#define	DM_HW_HANDLER_H

#include <linux/device-mapper.h>

#include "dm-mpath.h"

struct hw_handler_type;
struct hw_handler {
	struct hw_handler_type *type;
	void *context;
};

/*
 * Constructs a hardware handler object, takes custom arguments
 */
typedef int (*hwh_ctr_fn) (struct hw_handler *hwh, unsigned arc, char **argv);
typedef void (*hwh_dtr_fn) (struct hw_handler *hwh);

typedef void (*hwh_pg_init_fn) (struct hw_handler *hwh, unsigned bypassed,
				struct path *path);
typedef unsigned (*hwh_err_fn) (struct hw_handler *hwh, struct bio *bio);
typedef	int (*hwh_status_fn) (struct hw_handler *hwh,
			      status_type_t type,
			      char *result, unsigned int maxlen);

/* Information about a hardware handler type */
struct hw_handler_type {
	char *name;
	struct module *module;

	hwh_ctr_fn ctr;
	hwh_dtr_fn dtr;

	hwh_pg_init_fn pg_init;
	hwh_err_fn err;
	hwh_status_fn status;
};

/* Register a hardware handler */
int dm_register_hw_handler(struct hw_handler_type *type);

/* Unregister a hardware handler */
int dm_unregister_hw_handler(struct hw_handler_type *type);

/* Returns a registered hardware handler type */
struct hw_handler_type *dm_get_hw_handler(const char *name);

/* Releases a hardware handler  */
void dm_put_hw_handler(struct hw_handler_type *hwht);

/* Default hwh_err_fn */
unsigned dm_scsi_err_handler(struct hw_handler *hwh, struct bio *bio);

/* Error flags for hwh_err_fn and dm_pg_init_complete */
#define MP_FAIL_PATH 1
#define MP_BYPASS_PG 2
#define MP_ERROR_IO  4	/* Don't retry this I/O */

#endif
