// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_parser.h>
#include <linux/rculist.h>

/*
 * Read-mostly filesystem drivers list.
 *
 * Readers walk under rcu_read_lock(); writers take file_systems_lock
 * and publish via _rcu hlist primitives.  unregister_filesystem()
 * synchronize_rcu()s after unlock so the embedded file_system_type
 * can't go away under a reader.  To keep using a filesystem after
 * the RCU section ends, take a module reference via try_module_get().
 */
static HLIST_HEAD(file_systems);
static DEFINE_SPINLOCK(file_systems_lock);

/* WARNING: This can be used only if we _already_ own a reference */
struct file_system_type *get_filesystem(struct file_system_type *fs)
{
	__module_get(fs->owner);
	return fs;
}

void put_filesystem(struct file_system_type *fs)
{
	module_put(fs->owner);
}

static struct file_system_type *find_filesystem(const char *name, unsigned len)
{
	struct file_system_type *fs;

	hlist_for_each_entry_rcu(fs, &file_systems, list,
				 lockdep_is_held(&file_systems_lock))
		if (strncmp(fs->name, name, len) == 0 && !fs->name[len])
			return fs;
	return NULL;
}

/**
 *	register_filesystem - register a new filesystem
 *	@fs: the file system structure
 *
 *	Adds the file system passed to the list of file systems the kernel
 *	is aware of for mount and other syscalls. Returns 0 on success,
 *	or a negative errno code on an error.
 *
 *	The &struct file_system_type that is passed is linked into the kernel
 *	structures and must not be freed until the file system has been
 *	unregistered.
 */
int register_filesystem(struct file_system_type *fs)
{
	if (fs->parameters &&
	    !fs_validate_description(fs->name, fs->parameters))
		return -EINVAL;

	BUG_ON(strchr(fs->name, '.'));
	if (!hlist_unhashed_lockless(&fs->list))
		return -EBUSY;

	guard(spinlock)(&file_systems_lock);
	if (find_filesystem(fs->name, strlen(fs->name)))
		return -EBUSY;
	hlist_add_tail_rcu(&fs->list, &file_systems);
	return 0;
}
EXPORT_SYMBOL(register_filesystem);

/**
 *	unregister_filesystem - unregister a file system
 *	@fs: filesystem to unregister
 *
 *	Remove a file system that was previously successfully registered
 *	with the kernel. An error is returned if the file system is not found.
 *	Zero is returned on a success.
 *
 *	Once this function has returned the &struct file_system_type structure
 *	may be freed or reused.
 */
int unregister_filesystem(struct file_system_type *fs)
{
	scoped_guard(spinlock, &file_systems_lock) {
		if (hlist_unhashed(&fs->list))
			return -EINVAL;
		hlist_del_init_rcu(&fs->list);
	}
	synchronize_rcu();
	return 0;
}
EXPORT_SYMBOL(unregister_filesystem);

#ifdef CONFIG_SYSFS_SYSCALL
static int fs_index(const char __user *__name)
{
	struct file_system_type *p;
	char *name __free(kfree) = strndup_user(__name, PATH_MAX);
	int index = 0;

	if (IS_ERR(name))
		return PTR_ERR(name);

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		if (strcmp(p->name, name) == 0)
			return index;
		index++;
	}
	return -EINVAL;
}

static int fs_name(unsigned int index, char __user *buf)
{
	struct file_system_type *p, *found = NULL;
	int len, res;

	scoped_guard(rcu) {
		hlist_for_each_entry_rcu(p, &file_systems, list) {
			if (index--)
				continue;
			if (try_module_get(p->owner))
				found = p;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	/* OK, we got the reference, so we can safely block */
	len = strlen(found->name) + 1;
	res = copy_to_user(buf, found->name, len) ? -EFAULT : 0;
	put_filesystem(found);
	return res;
}

static int fs_maxindex(void)
{
	struct file_system_type *p;
	int index = 0;

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list)
		index++;
	return index;
}

/*
 * Whee.. Weird sysv syscall.
 */
SYSCALL_DEFINE3(sysfs, int, option, unsigned long, arg1, unsigned long, arg2)
{
	int retval = -EINVAL;

	switch (option) {
		case 1:
			retval = fs_index((const char __user *) arg1);
			break;

		case 2:
			retval = fs_name(arg1, (char __user *) arg2);
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	return retval;
}
#endif

int __init list_bdev_fs_names(char *buf, size_t size)
{
	struct file_system_type *p;
	size_t len;
	int count = 0;

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		if (!(p->fs_flags & FS_REQUIRES_DEV))
			continue;
		len = strlen(p->name) + 1;
		if (len > size) {
			pr_warn("%s: truncating file system list\n", __func__);
			break;
		}
		memcpy(buf, p->name, len);
		buf += len;
		size -= len;
		count++;
	}
	return count;
}

#ifdef CONFIG_PROC_FS
static int filesystems_proc_show(struct seq_file *m, void *v)
{
	struct file_system_type *p;

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		seq_printf(m, "%s\t%s\n",
			   (p->fs_flags & FS_REQUIRES_DEV) ? "" : "nodev",
			   p->name);
	}
	return 0;
}

static int __init proc_filesystems_init(void)
{
	proc_create_single("filesystems", 0, NULL, filesystems_proc_show);
	return 0;
}
module_init(proc_filesystems_init);
#endif

static struct file_system_type *__get_fs_type(const char *name, int len)
{
	struct file_system_type *fs;

	guard(rcu)();
	fs = find_filesystem(name, len);
	if (fs && !try_module_get(fs->owner))
		fs = NULL;
	return fs;
}

struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type *fs;
	const char *dot = strchr(name, '.');
	int len = dot ? dot - name : strlen(name);

	fs = __get_fs_type(name, len);
	if (!fs && (request_module("fs-%.*s", len, name) == 0)) {
		fs = __get_fs_type(name, len);
		if (!fs)
			pr_warn_once("request_module fs-%.*s succeeded, but still no fs?\n",
				     len, name);
	}

	if (dot && fs && !(fs->fs_flags & FS_HAS_SUBTYPE)) {
		put_filesystem(fs);
		fs = NULL;
	}
	return fs;
}
EXPORT_SYMBOL(get_fs_type);
