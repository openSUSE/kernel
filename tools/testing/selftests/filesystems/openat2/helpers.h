// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2018-2019 SUSE LLC.
 * Copyright (C) 2026 Amutable GmbH
 */

#ifndef __RESOLVEAT_H__
#define __RESOLVEAT_H__

#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include "kselftest.h"

#define BUILD_BUG_ON(e) ((void)(sizeof(struct { int:(-!!(e)); })))

/*
 * Arguments for how openat2(2) should open the target path. If @resolve is
 * zero, then openat2(2) operates very similarly to openat(2).
 *
 * However, unlike openat(2), unknown bits in @flags result in -EINVAL rather
 * than being silently ignored. @mode must be zero unless one of {O_CREAT,
 * O_TMPFILE} are set.
 *
 * @flags: O_* flags.
 * @mode: O_CREAT/O_TMPFILE file mode.
 * @resolve: RESOLVE_* flags.
 */
struct open_how {
	__u64 flags;
	__u64 mode;
	__u64 resolve;
};

#define OPEN_HOW_SIZE_VER0	24 /* sizeof first published struct */
#define OPEN_HOW_SIZE_LATEST	OPEN_HOW_SIZE_VER0

#ifndef RESOLVE_IN_ROOT
/* how->resolve flags for openat2(2). */
#define RESOLVE_NO_XDEV		0x01 /* Block mount-point crossings
					(includes bind-mounts). */
#define RESOLVE_NO_MAGICLINKS	0x02 /* Block traversal through procfs-style
					"magic-links". */
#define RESOLVE_NO_SYMLINKS	0x04 /* Block traversal through all symlinks
					(implies OEXT_NO_MAGICLINKS) */
#define RESOLVE_BENEATH		0x08 /* Block "lexical" trickery like
					"..", symlinks, and absolute
					paths which escape the dirfd. */
#define RESOLVE_IN_ROOT		0x10 /* Make all jumps to "/" and ".."
					be scoped inside the dirfd
					(similar to chroot(2)). */
#endif /* RESOLVE_IN_ROOT */

#define E_func(func, ...)						      \
	do {								      \
		errno = 0;						      \
		if (func(__VA_ARGS__) < 0)				      \
			ksft_exit_fail_msg("%s:%d %s failed - errno:%d\n",    \
					   __FILE__, __LINE__, #func, errno); \
	} while (0)

#define E_asprintf(...)		E_func(asprintf,	__VA_ARGS__)
#define E_chmod(...)		E_func(chmod,		__VA_ARGS__)
#define E_dup2(...)		E_func(dup2,		__VA_ARGS__)
#define E_fchdir(...)		E_func(fchdir,		__VA_ARGS__)
#define E_fstatat(...)		E_func(fstatat,		__VA_ARGS__)
#define E_kill(...)		E_func(kill,		__VA_ARGS__)
#define E_mkdirat(...)		E_func(mkdirat,		__VA_ARGS__)
#define E_mount(...)		E_func(mount,		__VA_ARGS__)
#define E_prctl(...)		E_func(prctl,		__VA_ARGS__)
#define E_readlink(...)		E_func(readlink,	__VA_ARGS__)
#define E_setresuid(...)	E_func(setresuid,	__VA_ARGS__)
#define E_symlinkat(...)	E_func(symlinkat,	__VA_ARGS__)
#define E_touchat(...)		E_func(touchat,		__VA_ARGS__)
#define E_unshare(...)		E_func(unshare,		__VA_ARGS__)

#define E_assert(expr, msg, ...)					\
	do {								\
		if (!(expr))						\
			ksft_exit_fail_msg("ASSERT(%s:%d) failed (%s): " msg "\n", \
					   __FILE__, __LINE__, #expr, ##__VA_ARGS__); \
	} while (0)

__maybe_unused
static bool needs_openat2(const struct open_how *how)
{
	return how->resolve != 0;
}

__maybe_unused
static int raw_openat2(int dfd, const char *path, void *how, size_t size)
{
	int ret = syscall(__NR_openat2, dfd, path, how, size);

	return ret >= 0 ? ret : -errno;
}

__maybe_unused
static int sys_openat2(int dfd, const char *path, struct open_how *how)
{
	return raw_openat2(dfd, path, how, sizeof(*how));
}

__maybe_unused
static int sys_openat(int dfd, const char *path, struct open_how *how)
{
	int ret = openat(dfd, path, how->flags, how->mode);

	return ret >= 0 ? ret : -errno;
}

__maybe_unused
static int sys_renameat2(int olddirfd, const char *oldpath,
			 int newdirfd, const char *newpath, unsigned int flags)
{
	int ret = syscall(__NR_renameat2, olddirfd, oldpath,
					  newdirfd, newpath, flags);

	return ret >= 0 ? ret : -errno;
}

__maybe_unused
static int touchat(int dfd, const char *path)
{
	int fd = openat(dfd, path, O_CREAT, 0700);

	if (fd >= 0)
		close(fd);
	return fd;
}

__maybe_unused
static char *fdreadlink(int fd)
{
	char *target, *tmp;

	E_asprintf(&tmp, "/proc/self/fd/%d", fd);

	target = malloc(PATH_MAX);
	if (!target)
		ksft_exit_fail_msg("fdreadlink: malloc failed\n");
	memset(target, 0, PATH_MAX);

	E_readlink(tmp, target, PATH_MAX);
	free(tmp);
	return target;
}

__maybe_unused
static bool fdequal(int fd, int dfd, const char *path)
{
	char *fdpath, *dfdpath, *other;
	bool cmp;

	fdpath = fdreadlink(fd);
	dfdpath = fdreadlink(dfd);

	if (!path)
		E_asprintf(&other, "%s", dfdpath);
	else if (*path == '/')
		E_asprintf(&other, "%s", path);
	else
		E_asprintf(&other, "%s/%s", dfdpath, path);

	cmp = !strcmp(fdpath, other);

	free(fdpath);
	free(dfdpath);
	free(other);
	return cmp;
}

static bool openat2_supported = false;

__attribute__((constructor))
static void __detect_openat2_supported(void)
{
	struct open_how how = {};
	int fd;

	BUILD_BUG_ON(sizeof(struct open_how) != OPEN_HOW_SIZE_VER0);

	/* Check openat2(2) support. */
	fd = sys_openat2(AT_FDCWD, ".", &how);
	openat2_supported = (fd >= 0);

	if (fd >= 0)
		close(fd);
}

#endif /* __RESOLVEAT_H__ */
