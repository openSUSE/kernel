// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/errno.h>
#include <linux/sed-opal-key.h>

int __weak sed_read_key(char *keyname, char *key, u_int *keylen)
{
	return -EOPNOTSUPP;
}

int __weak sed_write_key(char *keyname, char *key, u_int keylen)
{
	return -EOPNOTSUPP;
}
