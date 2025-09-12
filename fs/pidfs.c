// SPDX-License-Identifier: GPL-2.0
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/cgroup.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/pid.h>
#include <linux/pidfs.h>
#include <linux/pid_namespace.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/pseudo_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <uapi/linux/pidfd.h>
#include <linux/ipc_namespace.h>
#include <linux/time_namespace.h>
#include <linux/utsname.h>
#include <net/net_namespace.h>

#include "internal.h"
#include "mount.h"

static struct kmem_cache *pidfs_cachep __ro_after_init;

/*
 * Stashes information that userspace needs to access even after the
 * process has been reaped.
 */
struct pidfs_exit_info {
	__u64 cgroupid;
	__s32 exit_code;
};

struct pidfs_inode {
	struct pidfs_exit_info __pei;
	struct pidfs_exit_info *exit_info;
	struct inode vfs_inode;
};

static inline struct pidfs_inode *pidfs_i(struct inode *inode)
{
	return container_of(inode, struct pidfs_inode, vfs_inode);
}

#ifdef CONFIG_PROC_FS
/**
 * pidfd_show_fdinfo - print information about a pidfd
 * @m: proc fdinfo file
 * @f: file referencing a pidfd
 *
 * Pid:
 * This function will print the pid that a given pidfd refers to in the
 * pid namespace of the procfs instance.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its pid. This is
 * similar to calling getppid() on a process whose parent is outside of
 * its pid namespace.
 *
 * NSpid:
 * If pid namespaces are supported then this function will also print
 * the pid of a given pidfd refers to for all descendant pid namespaces
 * starting from the current pid namespace of the instance, i.e. the
 * Pid field and the first entry in the NSpid field will be identical.
 * If the pid namespace of the process is not a descendant of the pid
 * namespace of the procfs instance 0 will be shown as its first NSpid
 * entry and no others will be shown.
 * Note that this differs from the Pid and NSpid fields in
 * /proc/<pid>/status where Pid and NSpid are always shown relative to
 * the  pid namespace of the procfs instance. The difference becomes
 * obvious when sending around a pidfd between pid namespaces from a
 * different branch of the tree, i.e. where no ancestral relation is
 * present between the pid namespaces:
 * - create two new pid namespaces ns1 and ns2 in the initial pid
 *   namespace (also take care to create new mount namespaces in the
 *   new pid namespace and mount procfs)
 * - create a process with a pidfd in ns1
 * - send pidfd from ns1 to ns2
 * - read /proc/self/fdinfo/<pidfd> and observe that both Pid and NSpid
 *   have exactly one entry, which is 0
 */
static void pidfd_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct pid *pid = pidfd_pid(f);
	struct pid_namespace *ns;
	pid_t nr = -1;

	if (likely(pid_has_task(pid, PIDTYPE_PID))) {
		ns = proc_pid_ns(file_inode(m->file)->i_sb);
		nr = pid_nr_ns(pid, ns);
	}

	seq_put_decimal_ll(m, "Pid:\t", nr);

#ifdef CONFIG_PID_NS
	seq_put_decimal_ll(m, "\nNSpid:\t", nr);
	if (nr > 0) {
		int i;

		/* If nr is non-zero it means that 'pid' is valid and that
		 * ns, i.e. the pid namespace associated with the procfs
		 * instance, is in the pid namespace hierarchy of pid.
		 * Start at one below the already printed level.
		 */
		for (i = ns->level + 1; i <= pid->level; i++)
			seq_put_decimal_ll(m, "\t", pid->numbers[i].nr);
	}
#endif
	seq_putc(m, '\n');
}
#endif

/*
 * Poll support for process exit notification.
 */
static __poll_t pidfd_poll(struct file *file, struct poll_table_struct *pts)
{
	struct pid *pid = pidfd_pid(file);
	struct task_struct *task;
	__poll_t poll_flags = 0;

	poll_wait(file, &pid->wait_pidfd, pts);
	/*
	 * Don't wake waiters if the thread-group leader exited
	 * prematurely. They either get notified when the last subthread
	 * exits or not at all if one of the remaining subthreads execs
	 * and assumes the struct pid of the old thread-group leader.
	 */
	guard(rcu)();
	task = pid_task(pid, PIDTYPE_PID);
	if (!task)
		poll_flags = EPOLLIN | EPOLLRDNORM | EPOLLHUP;
	else if (task->exit_state && !delay_group_leader(task))
		poll_flags = EPOLLIN | EPOLLRDNORM;

	return poll_flags;
}

static inline bool pid_in_current_pidns(const struct pid *pid)
{
	const struct pid_namespace *ns = task_active_pid_ns(current);

	if (ns->level <= pid->level)
		return pid->numbers[ns->level].ns == ns;

	return false;
}

static long pidfd_info(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pidfd_info __user *uinfo = (struct pidfd_info __user *)arg;
	struct inode *inode = file_inode(file);
	struct pid *pid = pidfd_pid(file);
	size_t usize = _IOC_SIZE(cmd);
	struct pidfd_info kinfo = {};
	struct pidfs_exit_info *exit_info;
	struct user_namespace *user_ns;
	struct task_struct *task;
	const struct cred *c;
	__u64 mask;

	if (!uinfo)
		return -EINVAL;
	if (usize < PIDFD_INFO_SIZE_VER0)
		return -EINVAL; /* First version, no smaller struct possible */

	if (copy_from_user(&mask, &uinfo->mask, sizeof(mask)))
		return -EFAULT;

	/*
	 * Restrict information retrieval to tasks within the caller's pid
	 * namespace hierarchy.
	 */
	if (!pid_in_current_pidns(pid))
		return -ESRCH;

	if (mask & PIDFD_INFO_EXIT) {
		exit_info = READ_ONCE(pidfs_i(inode)->exit_info);
		if (exit_info) {
			kinfo.mask |= PIDFD_INFO_EXIT;
#ifdef CONFIG_CGROUPS
			kinfo.cgroupid = exit_info->cgroupid;
			kinfo.mask |= PIDFD_INFO_CGROUPID;
#endif
			kinfo.exit_code = exit_info->exit_code;
		}
	}

	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task) {
		/*
		 * If the task has already been reaped, only exit
		 * information is available
		 */
		if (!(mask & PIDFD_INFO_EXIT))
			return -ESRCH;

		goto copy_out;
	}

	c = get_task_cred(task);
	if (!c)
		return -ESRCH;

	/* Unconditionally return identifiers and credentials, the rest only on request */

	user_ns = current_user_ns();
	kinfo.ruid = from_kuid_munged(user_ns, c->uid);
	kinfo.rgid = from_kgid_munged(user_ns, c->gid);
	kinfo.euid = from_kuid_munged(user_ns, c->euid);
	kinfo.egid = from_kgid_munged(user_ns, c->egid);
	kinfo.suid = from_kuid_munged(user_ns, c->suid);
	kinfo.sgid = from_kgid_munged(user_ns, c->sgid);
	kinfo.fsuid = from_kuid_munged(user_ns, c->fsuid);
	kinfo.fsgid = from_kgid_munged(user_ns, c->fsgid);
	kinfo.mask |= PIDFD_INFO_CREDS;
	put_cred(c);

#ifdef CONFIG_CGROUPS
	if (!kinfo.cgroupid) {
		struct cgroup *cgrp;

		rcu_read_lock();
		cgrp = task_dfl_cgroup(task);
		kinfo.cgroupid = cgroup_id(cgrp);
		kinfo.mask |= PIDFD_INFO_CGROUPID;
		rcu_read_unlock();
	}
#endif

	/*
	 * Copy pid/tgid last, to reduce the chances the information might be
	 * stale. Note that it is not possible to ensure it will be valid as the
	 * task might return as soon as the copy_to_user finishes, but that's ok
	 * and userspace expects that might happen and can act accordingly, so
	 * this is just best-effort. What we can do however is checking that all
	 * the fields are set correctly, or return ESRCH to avoid providing
	 * incomplete information. */

	kinfo.ppid = task_ppid_nr_ns(task, NULL);
	kinfo.tgid = task_tgid_vnr(task);
	kinfo.pid = task_pid_vnr(task);
	kinfo.mask |= PIDFD_INFO_PID;

	if (kinfo.pid == 0 || kinfo.tgid == 0)
		return -ESRCH;

copy_out:
	/*
	 * If userspace and the kernel have the same struct size it can just
	 * be copied. If userspace provides an older struct, only the bits that
	 * userspace knows about will be copied. If userspace provides a new
	 * struct, only the bits that the kernel knows about will be copied.
	 */
	return copy_struct_to_user(uinfo, usize, &kinfo, sizeof(kinfo), NULL);
}

static long pidfd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct task_struct *task __free(put_task) = NULL;
	struct nsproxy *nsp __free(put_nsproxy) = NULL;
	struct ns_common *ns_common = NULL;
	struct pid_namespace *pid_ns;

	/* Extensible IOCTL that does not open namespace FDs, take a shortcut */
	if (_IOC_NR(cmd) == _IOC_NR(PIDFD_GET_INFO))
		return pidfd_info(file, cmd, arg);

	task = get_pid_task(pidfd_pid(file), PIDTYPE_PID);
	if (!task)
		return -ESRCH;

	if (arg)
		return -EINVAL;

	scoped_guard(task_lock, task) {
		nsp = task->nsproxy;
		if (nsp)
			get_nsproxy(nsp);
	}
	if (!nsp)
		return -ESRCH; /* just pretend it didn't exist */

	/*
	 * We're trying to open a file descriptor to the namespace so perform a
	 * filesystem cred ptrace check. Also, we mirror nsfs behavior.
	 */
	if (!ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS))
		return -EACCES;

	switch (cmd) {
	/* Namespaces that hang of nsproxy. */
	case PIDFD_GET_CGROUP_NAMESPACE:
		if (IS_ENABLED(CONFIG_CGROUPS)) {
			get_cgroup_ns(nsp->cgroup_ns);
			ns_common = to_ns_common(nsp->cgroup_ns);
		}
		break;
	case PIDFD_GET_IPC_NAMESPACE:
		if (IS_ENABLED(CONFIG_IPC_NS)) {
			get_ipc_ns(nsp->ipc_ns);
			ns_common = to_ns_common(nsp->ipc_ns);
		}
		break;
	case PIDFD_GET_MNT_NAMESPACE:
		get_mnt_ns(nsp->mnt_ns);
		ns_common = to_ns_common(nsp->mnt_ns);
		break;
	case PIDFD_GET_NET_NAMESPACE:
		if (IS_ENABLED(CONFIG_NET_NS)) {
			ns_common = to_ns_common(nsp->net_ns);
			get_net_ns(ns_common);
		}
		break;
	case PIDFD_GET_PID_FOR_CHILDREN_NAMESPACE:
		if (IS_ENABLED(CONFIG_PID_NS)) {
			get_pid_ns(nsp->pid_ns_for_children);
			ns_common = to_ns_common(nsp->pid_ns_for_children);
		}
		break;
	case PIDFD_GET_TIME_NAMESPACE:
		if (IS_ENABLED(CONFIG_TIME_NS)) {
			get_time_ns(nsp->time_ns);
			ns_common = to_ns_common(nsp->time_ns);
		}
		break;
	case PIDFD_GET_TIME_FOR_CHILDREN_NAMESPACE:
		if (IS_ENABLED(CONFIG_TIME_NS)) {
			get_time_ns(nsp->time_ns_for_children);
			ns_common = to_ns_common(nsp->time_ns_for_children);
		}
		break;
	case PIDFD_GET_UTS_NAMESPACE:
		if (IS_ENABLED(CONFIG_UTS_NS)) {
			get_uts_ns(nsp->uts_ns);
			ns_common = to_ns_common(nsp->uts_ns);
		}
		break;
	/* Namespaces that don't hang of nsproxy. */
	case PIDFD_GET_USER_NAMESPACE:
		if (IS_ENABLED(CONFIG_USER_NS)) {
			rcu_read_lock();
			ns_common = to_ns_common(get_user_ns(task_cred_xxx(task, user_ns)));
			rcu_read_unlock();
		}
		break;
	case PIDFD_GET_PID_NAMESPACE:
		if (IS_ENABLED(CONFIG_PID_NS)) {
			rcu_read_lock();
			pid_ns = task_active_pid_ns(task);
			if (pid_ns)
				ns_common = to_ns_common(get_pid_ns(pid_ns));
			rcu_read_unlock();
		}
		break;
	default:
		return -ENOIOCTLCMD;
	}

	if (!ns_common)
		return -EOPNOTSUPP;

	/* open_namespace() unconditionally consumes the reference */
	return open_namespace(ns_common);
}

static const struct file_operations pidfs_file_operations = {
	.poll		= pidfd_poll,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= pidfd_show_fdinfo,
#endif
	.unlocked_ioctl	= pidfd_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

struct pid *pidfd_pid(const struct file *file)
{
	if (file->f_op != &pidfs_file_operations)
		return ERR_PTR(-EBADF);
	return file_inode(file)->i_private;
}

/*
 * We're called from release_task(). We know there's at least one
 * reference to struct pid being held that won't be released until the
 * task has been reaped which cannot happen until we're out of
 * release_task().
 *
 * If this struct pid is referred to by a pidfd then
 * stashed_dentry_get() will return the dentry and inode for that struct
 * pid. Since we've taken a reference on it there's now an additional
 * reference from the exit path on it. Which is fine. We're going to put
 * it again in a second and we know that the pid is kept alive anyway.
 *
 * Worst case is that we've filled in the info and immediately free the
 * dentry and inode afterwards since the pidfd has been closed. Since
 * pidfs_exit() currently is placed after exit_task_work() we know that
 * it cannot be us aka the exiting task holding a pidfd to ourselves.
 */
void pidfs_exit(struct task_struct *tsk)
{
	struct dentry *dentry;

	might_sleep();

	dentry = stashed_dentry_get(&task_pid(tsk)->stashed);
	if (dentry) {
		struct inode *inode = d_inode(dentry);
		struct pidfs_exit_info *exit_info = &pidfs_i(inode)->__pei;
#ifdef CONFIG_CGROUPS
		struct cgroup *cgrp;

		rcu_read_lock();
		cgrp = task_dfl_cgroup(tsk);
		exit_info->cgroupid = cgroup_id(cgrp);
		rcu_read_unlock();
#endif
		exit_info->exit_code = tsk->exit_code;

		/* Ensure that PIDFD_GET_INFO sees either all or nothing. */
		smp_store_release(&pidfs_i(inode)->exit_info, &pidfs_i(inode)->__pei);
		dput(dentry);
	}
}

static struct vfsmount *pidfs_mnt __ro_after_init;

#if BITS_PER_LONG == 32
/*
 * Provide a fallback mechanism for 32-bit systems so processes remain
 * reliably comparable by inode number even on those systems.
 */
static DEFINE_IDA(pidfd_inum_ida);

static int pidfs_inum(struct pid *pid, unsigned long *ino)
{
	int ret;

	ret = ida_alloc_range(&pidfd_inum_ida, RESERVED_PIDS + 1,
			      UINT_MAX, GFP_ATOMIC);
	if (ret < 0)
		return -ENOSPC;

	*ino = ret;
	return 0;
}

static inline void pidfs_free_inum(unsigned long ino)
{
	if (ino > 0)
		ida_free(&pidfd_inum_ida, ino);
}
#else
static inline int pidfs_inum(struct pid *pid, unsigned long *ino)
{
	*ino = pid->ino;
	return 0;
}
#define pidfs_free_inum(ino) ((void)(ino))
#endif

/*
 * The vfs falls back to simple_setattr() if i_op->setattr() isn't
 * implemented. Let's reject it completely until we have a clean
 * permission concept for pidfds.
 */
static int pidfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			 struct iattr *attr)
{
	return -EOPNOTSUPP;
}


/*
 * User space expects pidfs inodes to have no file type in st_mode.
 *
 * In particular, 'lsof' has this legacy logic:
 *
 *	type = s->st_mode & S_IFMT;
 *	switch (type) {
 *	  ...
 *	case 0:
 *		if (!strcmp(p, "anon_inode"))
 *			Lf->ntype = Ntype = N_ANON_INODE;
 *
 * to detect our old anon_inode logic.
 *
 * Rather than mess with our internal sane inode data, just fix it
 * up here in getattr() by masking off the format bits.
 */
static int pidfs_getattr(struct mnt_idmap *idmap, const struct path *path,
			 struct kstat *stat, u32 request_mask,
			 unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);

	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	stat->mode &= ~S_IFMT;
	return 0;
}

static const struct inode_operations pidfs_inode_operations = {
	.getattr = pidfs_getattr,
	.setattr = pidfs_setattr,
};

static void pidfs_evict_inode(struct inode *inode)
{
	struct pid *pid = inode->i_private;

	clear_inode(inode);
	put_pid(pid);
	pidfs_free_inum(inode->i_ino);
}

static struct inode *pidfs_alloc_inode(struct super_block *sb)
{
	struct pidfs_inode *pi;

	pi = alloc_inode_sb(sb, pidfs_cachep, GFP_KERNEL);
	if (!pi)
		return NULL;

	memset(&pi->__pei, 0, sizeof(pi->__pei));
	pi->exit_info = NULL;

	return &pi->vfs_inode;
}

static void pidfs_free_inode(struct inode *inode)
{
	kmem_cache_free(pidfs_cachep, pidfs_i(inode));
}

static const struct super_operations pidfs_sops = {
	.alloc_inode	= pidfs_alloc_inode,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= pidfs_evict_inode,
	.free_inode	= pidfs_free_inode,
	.statfs		= simple_statfs,
};

/*
 * 'lsof' has knowledge of out historical anon_inode use, and expects
 * the pidfs dentry name to start with 'anon_inode'.
 */
static char *pidfs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(buffer, buflen, "anon_inode:[pidfd]");
}

static const struct dentry_operations pidfs_dentry_operations = {
	.d_delete	= always_delete_dentry,
	.d_dname	= pidfs_dname,
	.d_prune	= stashed_dentry_prune,
};

static inline bool pidfs_pid_valid(struct pid *pid, const struct path *path,
				   unsigned int flags)
{
	enum pid_type type;

	if (flags & PIDFD_CLONE)
		return true;

	/*
	 * Make sure that if a pidfd is created PIDFD_INFO_EXIT
	 * information will be available. So after an inode for the
	 * pidfd has been allocated perform another check that the pid
	 * is still alive. If it is exit information is available even
	 * if the task gets reaped before the pidfd is returned to
	 * userspace. The only exception is PIDFD_CLONE where no task
	 * linkage has been established for @pid yet and the kernel is
	 * in the middle of process creation so there's nothing for
	 * pidfs to miss.
	 */
	if (flags & PIDFD_THREAD)
		type = PIDTYPE_PID;
	else
		type = PIDTYPE_TGID;

	/*
	 * Since pidfs_exit() is called before struct pid's task linkage
	 * is removed the case where the task got reaped but a dentry
	 * was already attached to struct pid and exit information was
	 * recorded and published can be handled correctly.
	 */
	if (unlikely(!pid_has_task(pid, type))) {
		struct inode *inode = d_inode(path->dentry);
		return !!READ_ONCE(pidfs_i(inode)->exit_info);
	}

	return true;
}

static int pidfs_init_inode(struct inode *inode, void *data)
{
	inode->i_private = data;
	inode->i_flags |= S_PRIVATE;
	inode->i_mode |= S_IRWXU;
	inode->i_op = &pidfs_inode_operations;
	inode->i_fop = &pidfs_file_operations;
	/*
	 * Inode numbering for pidfs start at RESERVED_PIDS + 1. This
	 * avoids collisions with the root inode which is 1 for pseudo
	 * filesystems.
	 */
	return pidfs_inum(data, &inode->i_ino);
}

static void pidfs_put_data(void *data)
{
	struct pid *pid = data;
	put_pid(pid);
}

static const struct stashed_operations pidfs_stashed_ops = {
	.init_inode = pidfs_init_inode,
	.put_data = pidfs_put_data,
};

static int pidfs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx;

	ctx = init_pseudo(fc, PID_FS_MAGIC);
	if (!ctx)
		return -ENOMEM;

	fc->s_iflags |= SB_I_NOEXEC;
	fc->s_iflags |= SB_I_NODEV;
	ctx->ops = &pidfs_sops;
	ctx->dops = &pidfs_dentry_operations;
	fc->s_fs_info = (void *)&pidfs_stashed_ops;
	return 0;
}

static struct file_system_type pidfs_type = {
	.name			= "pidfs",
	.init_fs_context	= pidfs_init_fs_context,
	.kill_sb		= kill_anon_super,
};

struct file *pidfs_alloc_file(struct pid *pid, unsigned int flags)
{
	struct file *pidfd_file;
	struct path path __free(path_put) = {};
	int ret;

	/*
	 * Ensure that PIDFD_CLONE can be passed as a flag without
	 * overloading other uapi pidfd flags.
	 */
	BUILD_BUG_ON(PIDFD_CLONE == PIDFD_THREAD);
	BUILD_BUG_ON(PIDFD_CLONE == PIDFD_NONBLOCK);

	ret = path_from_stashed(&pid->stashed, pidfs_mnt, get_pid(pid), &path);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!pidfs_pid_valid(pid, &path, flags))
		return ERR_PTR(-ESRCH);

	flags &= ~PIDFD_CLONE;
	pidfd_file = dentry_open(&path, flags, current_cred());
	/* Raise PIDFD_THREAD explicitly as do_dentry_open() strips it. */
	if (!IS_ERR(pidfd_file))
		pidfd_file->f_flags |= (flags & PIDFD_THREAD);

	return pidfd_file;
}

static void pidfs_inode_init_once(void *data)
{
	struct pidfs_inode *pi = data;

	inode_init_once(&pi->vfs_inode);
}

void __init pidfs_init(void)
{
	pidfs_cachep = kmem_cache_create("pidfs_cache", sizeof(struct pidfs_inode), 0,
					 (SLAB_HWCACHE_ALIGN | SLAB_RECLAIM_ACCOUNT |
					  SLAB_ACCOUNT | SLAB_PANIC),
					 pidfs_inode_init_once);
	pidfs_mnt = kern_mount(&pidfs_type);
	if (IS_ERR(pidfs_mnt))
		panic("Failed to mount pidfs pseudo filesystem");
}
