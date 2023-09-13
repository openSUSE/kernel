/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SED key operations.
 *
 * Copyright (C) 2022 IBM Corporation
 *
 * These are the accessor functions (read/write) for SED Opal
 * keys. Specific keystores can provide overrides.
 *
 */

#include <linux/kernel.h>

int sed_read_key(char *keyname, char *key, u_int *keylen);
int sed_write_key(char *keyname, char *key, u_int keylen);
