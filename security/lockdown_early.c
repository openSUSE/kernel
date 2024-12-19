// SPDX-License-Identifier: GPL-2.0
/* Lock down flag of the kernel in early stage
 *
 * Copyright (c) 2024 SUSE LLC. All Rights Reserved.
 * Written by Joey Lee (jlee@suse.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
/* This is a a temporary solution. After the patch '77b644c39d6a init/main.c:
 * Initialize early LSMs after arch code, static keys and calls' be introduced
 * to v6.12 kernel. The early_security_init() be moved behine setup_arch().
 * It causes that thee original code of CONFIG_LOCK_DOWN_IN_EFI_SECURE_BOOT in
 * setup_arch() is invalid.
 *
 * This file includes two functions which are copied from
 * security/lockdown/lockdown.c and just simply modified for keeping the
 * original calling habits. For filling in the gap, I go back to use a lock
 * flag in early boot stage before the lockdown LSM be initial after
 * setup_arch(). The reason for creating a new C files instead of direct
 * modifing lockdown.c is to avoid compromising the security of lockdown LSM.
 *
 * This solution only be used in limited lock-down functions in setup_arch()
 * or even in early_initcall stage. I will removed this temporary solution
 * after the real solution shows on kernel mainline.
 */

#include <linux/security.h>

static enum lockdown_reason kernel_locked_down_early __ro_after_init;

static const enum lockdown_reason early_lockdown_levels[] = {LOCKDOWN_NONE,
						 LOCKDOWN_INTEGRITY_MAX,
						 LOCKDOWN_CONFIDENTIALITY_MAX};

int __init lock_kernel_down_early(const char *where, enum lockdown_reason level)
{
	if (kernel_locked_down_early >= level)
		return -EPERM;

	kernel_locked_down_early = level;
	pr_notice("Kernel is early locked down from %s; see man kernel_lockdown.7\n",
		  where);
	return 0;
}

int kernel_is_locked_down_early(int what)
{
	if (WARN(what >= LOCKDOWN_CONFIDENTIALITY_MAX,
		 "Invalid lockdown reason"))
		return -EPERM;

	if (kernel_locked_down_early >= what) {
		if (lockdown_reasons[what])
			pr_notice_ratelimited("Lockdown early: %s: %s is restricted; see man kernel_lockdown.7\n",
				  current->comm, lockdown_reasons[what]);
		return -EPERM;
	}

	return 0;
}
