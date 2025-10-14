// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002-2003 Nadia Yvette Chambers, IBM
 * (C) 2004 Nadia Yvette Chambers, Oracle
 * (C) 2002-2004 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 *
 * Pid namespaces:
 *    (C) 2007 Pavel Emelyanov <xemul@openvz.org>, OpenVZ, SWsoft Inc.
 *    (C) 2007 Sukadev Bhattiprolu <sukadev@us.ibm.com>, IBM
 *     Many thanks to Oleg Nesterov for comments and help
 *
 */

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <linux/memblock.h>
#include <linux/pid_namespace.h>
#include <linux/init_task.h>
#include <linux/syscalls.h>
#include <linux/proc_ns.h>
#include <linux/refcount.h>
#include <linux/anon_inodes.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/idr.h>
#include <linux/pidfs.h>
#include <linux/seqlock.h>
#include <net/sock.h>
#include <uapi/linux/pidfd.h>

struct pid init_struct_pid = {
	.count		= REFCOUNT_INIT(1),
	.tasks		= {
		{ .first = NULL },
		{ .first = NULL },
		{ .first = NULL },
	},
	.level		= 0,
	.numbers	= { {
		.nr		= 0,
		.ns		= &init_pid_ns,
	}, }
};

static int pid_max_min = RESERVED_PIDS + 1;
static int pid_max_max = PID_MAX_LIMIT;

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
struct pid_namespace init_pid_ns = {
	.ns.__ns_ref = REFCOUNT_INIT(2),
	.idr = IDR_INIT(init_pid_ns.idr),
	.pid_allocated = PIDNS_ADDING,
	.level = 0,
	.child_reaper = &init_task,
	.user_ns = &init_user_ns,
	.ns.inum = ns_init_inum(&init_pid_ns),
#ifdef CONFIG_PID_NS
	.ns.ops = &pidns_operations,
#endif
	.pid_max = PID_MAX_DEFAULT,
#if defined(CONFIG_SYSCTL) && defined(CONFIG_MEMFD_CREATE)
	.memfd_noexec_scope = MEMFD_NOEXEC_SCOPE_EXEC,
#endif
	.ns.ns_type = ns_common_type(&init_pid_ns),
};
EXPORT_SYMBOL_GPL(init_pid_ns);

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);
seqcount_spinlock_t pidmap_lock_seq = SEQCNT_SPINLOCK_ZERO(pidmap_lock_seq, &pidmap_lock);

void put_pid(struct pid *pid)
{
	struct pid_namespace *ns;

	if (!pid)
		return;

	ns = pid->numbers[pid->level].ns;
	if (refcount_dec_and_test(&pid->count)) {
		pidfs_free_pid(pid);
		kmem_cache_free(ns->pid_cachep, pid);
		put_pid_ns(ns);
	}
}
EXPORT_SYMBOL_GPL(put_pid);

static void delayed_put_pid(struct rcu_head *rhp)
{
	struct pid *pid = container_of(rhp, struct pid, rcu);
	put_pid(pid);
}

void free_pid(struct pid *pid)
{
	int i;

	lockdep_assert_not_held(&tasklist_lock);

	spin_lock(&pidmap_lock);
	for (i = 0; i <= pid->level; i++) {
		struct upid *upid = pid->numbers + i;
		struct pid_namespace *ns = upid->ns;
		switch (--ns->pid_allocated) {
		case 2:
		case 1:
			/* When all that is left in the pid namespace
			 * is the reaper wake up the reaper.  The reaper
			 * may be sleeping in zap_pid_ns_processes().
			 */
			wake_up_process(ns->child_reaper);
			break;
		case PIDNS_ADDING:
			/* Handle a fork failure of the first process */
			WARN_ON(ns->child_reaper);
			ns->pid_allocated = 0;
			break;
		}

		idr_remove(&ns->idr, upid->nr);
	}
	pidfs_remove_pid(pid);
	spin_unlock(&pidmap_lock);

	call_rcu(&pid->rcu, delayed_put_pid);
}

void free_pids(struct pid **pids)
{
	int tmp;

	/*
	 * This can batch pidmap_lock.
	 */
	for (tmp = PIDTYPE_MAX; --tmp >= 0; )
		if (pids[tmp])
			free_pid(pids[tmp]);
}

struct pid *alloc_pid(struct pid_namespace *ns, pid_t *set_tid,
		      size_t set_tid_size)
{
	struct pid *pid;
	enum pid_type type;
	int i, nr;
	struct pid_namespace *tmp;
	struct upid *upid;
	int retval = -ENOMEM;

	/*
	 * set_tid_size contains the size of the set_tid array. Starting at
	 * the most nested currently active PID namespace it tells alloc_pid()
	 * which PID to set for a process in that most nested PID namespace
	 * up to set_tid_size PID namespaces. It does not have to set the PID
	 * for a process in all nested PID namespaces but set_tid_size must
	 * never be greater than the current ns->level + 1.
	 */
	if (set_tid_size > ns->level + 1)
		return ERR_PTR(-EINVAL);

	pid = kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);
	if (!pid)
		return ERR_PTR(retval);

	tmp = ns;
	pid->level = ns->level;

	for (i = ns->level; i >= 0; i--) {
		int tid = 0;
		int pid_max = READ_ONCE(tmp->pid_max);

		if (set_tid_size) {
			tid = set_tid[ns->level - i];

			retval = -EINVAL;
			if (tid < 1 || tid >= pid_max)
				goto out_free;
			/*
			 * Also fail if a PID != 1 is requested and
			 * no PID 1 exists.
			 */
			if (tid != 1 && !tmp->child_reaper)
				goto out_free;
			retval = -EPERM;
			if (!checkpoint_restore_ns_capable(tmp->user_ns))
				goto out_free;
			set_tid_size--;
		}

		idr_preload(GFP_KERNEL);
		spin_lock(&pidmap_lock);

		if (tid) {
			nr = idr_alloc(&tmp->idr, NULL, tid,
				       tid + 1, GFP_ATOMIC);
			/*
			 * If ENOSPC is returned it means that the PID is
			 * alreay in use. Return EEXIST in that case.
			 */
			if (nr == -ENOSPC)
				nr = -EEXIST;
		} else {
			int pid_min = 1;
			/*
			 * init really needs pid 1, but after reaching the
			 * maximum wrap back to RESERVED_PIDS
			 */
			if (idr_get_cursor(&tmp->idr) > RESERVED_PIDS)
				pid_min = RESERVED_PIDS;

			/*
			 * Store a null pointer so find_pid_ns does not find
			 * a partially initialized PID (see below).
			 */
			nr = idr_alloc_cyclic(&tmp->idr, NULL, pid_min,
					      pid_max, GFP_ATOMIC);
		}
		spin_unlock(&pidmap_lock);
		idr_preload_end();

		if (nr < 0) {
			retval = (nr == -ENOSPC) ? -EAGAIN : nr;
			goto out_free;
		}

		pid->numbers[i].nr = nr;
		pid->numbers[i].ns = tmp;
		tmp = tmp->parent;
	}

	/*
	 * ENOMEM is not the most obvious choice especially for the case
	 * where the child subreaper has already exited and the pid
	 * namespace denies the creation of any new processes. But ENOMEM
	 * is what we have exposed to userspace for a long time and it is
	 * documented behavior for pid namespaces. So we can't easily
	 * change it even if there were an error code better suited.
	 */
	retval = -ENOMEM;

	get_pid_ns(ns);
	refcount_set(&pid->count, 1);
	spin_lock_init(&pid->lock);
	for (type = 0; type < PIDTYPE_MAX; ++type)
		INIT_HLIST_HEAD(&pid->tasks[type]);

	init_waitqueue_head(&pid->wait_pidfd);
	INIT_HLIST_HEAD(&pid->inodes);

	upid = pid->numbers + ns->level;
	idr_preload(GFP_KERNEL);
	spin_lock(&pidmap_lock);
	if (!(ns->pid_allocated & PIDNS_ADDING))
		goto out_unlock;
	pidfs_add_pid(pid);
	for ( ; upid >= pid->numbers; --upid) {
		/* Make the PID visible to find_pid_ns. */
		idr_replace(&upid->ns->idr, pid, upid->nr);
		upid->ns->pid_allocated++;
	}
	spin_unlock(&pidmap_lock);
	idr_preload_end();

	return pid;

out_unlock:
	spin_unlock(&pidmap_lock);
	idr_preload_end();
	put_pid_ns(ns);

out_free:
	spin_lock(&pidmap_lock);
	while (++i <= ns->level) {
		upid = pid->numbers + i;
		idr_remove(&upid->ns->idr, upid->nr);
	}

	/* On failure to allocate the first pid, reset the state */
	if (ns->pid_allocated == PIDNS_ADDING)
		idr_set_cursor(&ns->idr, 0);

	spin_unlock(&pidmap_lock);

	kmem_cache_free(ns->pid_cachep, pid);
	return ERR_PTR(retval);
}

void disable_pid_allocation(struct pid_namespace *ns)
{
	spin_lock(&pidmap_lock);
	ns->pid_allocated &= ~PIDNS_ADDING;
	spin_unlock(&pidmap_lock);
}

struct pid *find_pid_ns(int nr, struct pid_namespace *ns)
{
	return idr_find(&ns->idr, nr);
}
EXPORT_SYMBOL_GPL(find_pid_ns);

struct pid *find_vpid(int nr)
{
	return find_pid_ns(nr, task_active_pid_ns(current));
}
EXPORT_SYMBOL_GPL(find_vpid);

static struct pid **task_pid_ptr(struct task_struct *task, enum pid_type type)
{
	return (type == PIDTYPE_PID) ?
		&task->thread_pid :
		&task->signal->pids[type];
}

/*
 * attach_pid() must be called with the tasklist_lock write-held.
 */
void attach_pid(struct task_struct *task, enum pid_type type)
{
	struct pid *pid;

	lockdep_assert_held_write(&tasklist_lock);

	pid = *task_pid_ptr(task, type);
	hlist_add_head_rcu(&task->pid_links[type], &pid->tasks[type]);
}

static void __change_pid(struct pid **pids, struct task_struct *task,
			 enum pid_type type, struct pid *new)
{
	struct pid **pid_ptr, *pid;
	int tmp;

	lockdep_assert_held_write(&tasklist_lock);

	pid_ptr = task_pid_ptr(task, type);
	pid = *pid_ptr;

	hlist_del_rcu(&task->pid_links[type]);
	*pid_ptr = new;

	for (tmp = PIDTYPE_MAX; --tmp >= 0; )
		if (pid_has_task(pid, tmp))
			return;

	WARN_ON(pids[type]);
	pids[type] = pid;
}

void detach_pid(struct pid **pids, struct task_struct *task, enum pid_type type)
{
	__change_pid(pids, task, type, NULL);
}

void change_pid(struct pid **pids, struct task_struct *task, enum pid_type type,
		struct pid *pid)
{
	__change_pid(pids, task, type, pid);
	attach_pid(task, type);
}

void exchange_tids(struct task_struct *left, struct task_struct *right)
{
	struct pid *pid1 = left->thread_pid;
	struct pid *pid2 = right->thread_pid;
	struct hlist_head *head1 = &pid1->tasks[PIDTYPE_PID];
	struct hlist_head *head2 = &pid2->tasks[PIDTYPE_PID];

	lockdep_assert_held_write(&tasklist_lock);

	/* Swap the single entry tid lists */
	hlists_swap_heads_rcu(head1, head2);

	/* Swap the per task_struct pid */
	rcu_assign_pointer(left->thread_pid, pid2);
	rcu_assign_pointer(right->thread_pid, pid1);

	/* Swap the cached value */
	WRITE_ONCE(left->pid, pid_nr(pid2));
	WRITE_ONCE(right->pid, pid_nr(pid1));
}

/* transfer_pid is an optimization of attach_pid(new), detach_pid(old) */
void transfer_pid(struct task_struct *old, struct task_struct *new,
			   enum pid_type type)
{
	WARN_ON_ONCE(type == PIDTYPE_PID);
	lockdep_assert_held_write(&tasklist_lock);
	hlist_replace_rcu(&old->pid_links[type], &new->pid_links[type]);
}

struct task_struct *pid_task(struct pid *pid, enum pid_type type)
{
	struct task_struct *result = NULL;
	if (pid) {
		struct hlist_node *first;
		first = rcu_dereference_check(hlist_first_rcu(&pid->tasks[type]),
					      lockdep_tasklist_lock_is_held());
		if (first)
			result = hlist_entry(first, struct task_struct, pid_links[(type)]);
	}
	return result;
}
EXPORT_SYMBOL(pid_task);

/*
 * Must be called under rcu_read_lock().
 */
struct task_struct *find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "find_task_by_pid_ns() needs rcu_read_lock() protection");
	return pid_task(find_pid_ns(nr, ns), PIDTYPE_PID);
}

struct task_struct *find_task_by_vpid(pid_t vnr)
{
	return find_task_by_pid_ns(vnr, task_active_pid_ns(current));
}

struct task_struct *find_get_task_by_vpid(pid_t nr)
{
	struct task_struct *task;

	rcu_read_lock();
	task = find_task_by_vpid(nr);
	if (task)
		get_task_struct(task);
	rcu_read_unlock();

	return task;
}

struct pid *get_task_pid(struct task_struct *task, enum pid_type type)
{
	struct pid *pid;
	rcu_read_lock();
	pid = get_pid(rcu_dereference(*task_pid_ptr(task, type)));
	rcu_read_unlock();
	return pid;
}
EXPORT_SYMBOL_GPL(get_task_pid);

struct task_struct *get_pid_task(struct pid *pid, enum pid_type type)
{
	struct task_struct *result;
	rcu_read_lock();
	result = pid_task(pid, type);
	if (result)
		get_task_struct(result);
	rcu_read_unlock();
	return result;
}
EXPORT_SYMBOL_GPL(get_pid_task);

struct pid *find_get_pid(pid_t nr)
{
	struct pid *pid;

	rcu_read_lock();
	pid = get_pid(find_vpid(nr));
	rcu_read_unlock();

	return pid;
}
EXPORT_SYMBOL_GPL(find_get_pid);

pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns)
{
	struct upid *upid;
	pid_t nr = 0;

	if (pid && ns && ns->level <= pid->level) {
		upid = &pid->numbers[ns->level];
		if (upid->ns == ns)
			nr = upid->nr;
	}
	return nr;
}
EXPORT_SYMBOL_GPL(pid_nr_ns);

pid_t pid_vnr(struct pid *pid)
{
	return pid_nr_ns(pid, task_active_pid_ns(current));
}
EXPORT_SYMBOL_GPL(pid_vnr);

pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type,
			struct pid_namespace *ns)
{
	pid_t nr = 0;

	rcu_read_lock();
	if (!ns)
		ns = task_active_pid_ns(current);
	if (ns)
		nr = pid_nr_ns(rcu_dereference(*task_pid_ptr(task, type)), ns);
	rcu_read_unlock();

	return nr;
}
EXPORT_SYMBOL(__task_pid_nr_ns);

struct pid_namespace *task_active_pid_ns(struct task_struct *tsk)
{
	return ns_of_pid(task_pid(tsk));
}
EXPORT_SYMBOL_GPL(task_active_pid_ns);

/*
 * Used by proc to find the first pid that is greater than or equal to nr.
 *
 * If there is a pid at nr this function is exactly the same as find_pid_ns.
 */
struct pid *find_ge_pid(int nr, struct pid_namespace *ns)
{
	return idr_get_next(&ns->idr, &nr);
}
EXPORT_SYMBOL_GPL(find_ge_pid);

struct pid *pidfd_get_pid(unsigned int fd, unsigned int *flags)
{
	CLASS(fd, f)(fd);
	struct pid *pid;

	if (fd_empty(f))
		return ERR_PTR(-EBADF);

	pid = pidfd_pid(fd_file(f));
	if (!IS_ERR(pid)) {
		get_pid(pid);
		*flags = fd_file(f)->f_flags;
	}
	return pid;
}

/**
 * pidfd_get_task() - Get the task associated with a pidfd
 *
 * @pidfd: pidfd for which to get the task
 * @flags: flags associated with this pidfd
 *
 * Return the task associated with @pidfd. The function takes a reference on
 * the returned task. The caller is responsible for releasing that reference.
 *
 * Return: On success, the task_struct associated with the pidfd.
 *	   On error, a negative errno number will be returned.
 */
struct task_struct *pidfd_get_task(int pidfd, unsigned int *flags)
{
	unsigned int f_flags = 0;
	struct pid *pid;
	struct task_struct *task;
	enum pid_type type;

	switch (pidfd) {
	case  PIDFD_SELF_THREAD:
		type = PIDTYPE_PID;
		pid = get_task_pid(current, type);
		break;
	case  PIDFD_SELF_THREAD_GROUP:
		type = PIDTYPE_TGID;
		pid = get_task_pid(current, type);
		break;
	default:
		pid = pidfd_get_pid(pidfd, &f_flags);
		if (IS_ERR(pid))
			return ERR_CAST(pid);
		type = PIDTYPE_TGID;
		break;
	}

	task = get_pid_task(pid, type);
	put_pid(pid);
	if (!task)
		return ERR_PTR(-ESRCH);

	*flags = f_flags;
	return task;
}

/**
 * pidfd_create() - Create a new pid file descriptor.
 *
 * @pid:   struct pid that the pidfd will reference
 * @flags: flags to pass
 *
 * This creates a new pid file descriptor with the O_CLOEXEC flag set.
 *
 * Note, that this function can only be called after the fd table has
 * been unshared to avoid leaking the pidfd to the new process.
 *
 * This symbol should not be explicitly exported to loadable modules.
 *
 * Return: On success, a cloexec pidfd is returned.
 *         On error, a negative errno number will be returned.
 */
static int pidfd_create(struct pid *pid, unsigned int flags)
{
	int pidfd;
	struct file *pidfd_file;

	pidfd = pidfd_prepare(pid, flags, &pidfd_file);
	if (pidfd < 0)
		return pidfd;

	fd_install(pidfd, pidfd_file);
	return pidfd;
}

/**
 * sys_pidfd_open() - Open new pid file descriptor.
 *
 * @pid:   pid for which to retrieve a pidfd
 * @flags: flags to pass
 *
 * This creates a new pid file descriptor with the O_CLOEXEC flag set for
 * the task identified by @pid. Without PIDFD_THREAD flag the target task
 * must be a thread-group leader.
 *
 * Return: On success, a cloexec pidfd is returned.
 *         On error, a negative errno number will be returned.
 */
SYSCALL_DEFINE2(pidfd_open, pid_t, pid, unsigned int, flags)
{
	int fd;
	struct pid *p;

	if (flags & ~(PIDFD_NONBLOCK | PIDFD_THREAD))
		return -EINVAL;

	if (pid <= 0)
		return -EINVAL;

	p = find_get_pid(pid);
	if (!p)
		return -ESRCH;

	fd = pidfd_create(p, flags);

	put_pid(p);
	return fd;
}

#ifdef CONFIG_SYSCTL
static struct ctl_table_set *pid_table_root_lookup(struct ctl_table_root *root)
{
	return &task_active_pid_ns(current)->set;
}

static int set_is_seen(struct ctl_table_set *set)
{
	return &task_active_pid_ns(current)->set == set;
}

static int pid_table_root_permissions(struct ctl_table_header *head,
				      const struct ctl_table *table)
{
	struct pid_namespace *pidns =
		container_of(head->set, struct pid_namespace, set);
	int mode = table->mode;

	if (ns_capable_noaudit(pidns->user_ns, CAP_SYS_ADMIN) ||
	    uid_eq(current_euid(), make_kuid(pidns->user_ns, 0)))
		mode = (mode & S_IRWXU) >> 6;
	else if (in_egroup_p(make_kgid(pidns->user_ns, 0)))
		mode = (mode & S_IRWXG) >> 3;
	else
		mode = mode & S_IROTH;
	return (mode << 6) | (mode << 3) | mode;
}

static void pid_table_root_set_ownership(struct ctl_table_header *head,
					 kuid_t *uid, kgid_t *gid)
{
	struct pid_namespace *pidns =
		container_of(head->set, struct pid_namespace, set);
	kuid_t ns_root_uid;
	kgid_t ns_root_gid;

	ns_root_uid = make_kuid(pidns->user_ns, 0);
	if (uid_valid(ns_root_uid))
		*uid = ns_root_uid;

	ns_root_gid = make_kgid(pidns->user_ns, 0);
	if (gid_valid(ns_root_gid))
		*gid = ns_root_gid;
}

static struct ctl_table_root pid_table_root = {
	.lookup		= pid_table_root_lookup,
	.permissions	= pid_table_root_permissions,
	.set_ownership	= pid_table_root_set_ownership,
};

static int proc_do_cad_pid(const struct ctl_table *table, int write, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	struct pid *new_pid;
	pid_t tmp_pid;
	int r;
	struct ctl_table tmp_table = *table;

	tmp_pid = pid_vnr(cad_pid);
	tmp_table.data = &tmp_pid;

	r = proc_dointvec(&tmp_table, write, buffer, lenp, ppos);
	if (r || !write)
		return r;

	new_pid = find_get_pid(tmp_pid);
	if (!new_pid)
		return -ESRCH;

	put_pid(xchg(&cad_pid, new_pid));
	return 0;
}

static const struct ctl_table pid_table[] = {
	{
		.procname	= "pid_max",
		.data		= &init_pid_ns.pid_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &pid_max_min,
		.extra2		= &pid_max_max,
	},
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "cad_pid",
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_do_cad_pid,
	},
#endif
};
#endif

int register_pidns_sysctls(struct pid_namespace *pidns)
{
#ifdef CONFIG_SYSCTL
	struct ctl_table *tbl;

	setup_sysctl_set(&pidns->set, &pid_table_root, set_is_seen);

	tbl = kmemdup(pid_table, sizeof(pid_table), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;
	tbl->data = &pidns->pid_max;
	pidns->pid_max = min(pid_max_max, max_t(int, pidns->pid_max,
			     PIDS_PER_CPU_DEFAULT * num_possible_cpus()));

	pidns->sysctls = __register_sysctl_table(&pidns->set, "kernel", tbl,
						 ARRAY_SIZE(pid_table));
	if (!pidns->sysctls) {
		kfree(tbl);
		retire_sysctl_set(&pidns->set);
		return -ENOMEM;
	}
#endif
	return 0;
}

void unregister_pidns_sysctls(struct pid_namespace *pidns)
{
#ifdef CONFIG_SYSCTL
	const struct ctl_table *tbl;

	tbl = pidns->sysctls->ctl_table_arg;
	unregister_sysctl_table(pidns->sysctls);
	retire_sysctl_set(&pidns->set);
	kfree(tbl);
#endif
}

void __init pid_idr_init(void)
{
	/* Verify no one has done anything silly: */
	BUILD_BUG_ON(PID_MAX_LIMIT >= PIDNS_ADDING);

	/* bump default and minimum pid_max based on number of cpus */
	init_pid_ns.pid_max = min(pid_max_max, max_t(int, init_pid_ns.pid_max,
				  PIDS_PER_CPU_DEFAULT * num_possible_cpus()));
	pid_max_min = max_t(int, pid_max_min,
				PIDS_PER_CPU_MIN * num_possible_cpus());
	pr_info("pid_max: default: %u minimum: %u\n", init_pid_ns.pid_max, pid_max_min);

	idr_init(&init_pid_ns.idr);

	init_pid_ns.pid_cachep = kmem_cache_create("pid",
			struct_size_t(struct pid, numbers, 1),
			__alignof__(struct pid),
			SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT,
			NULL);
}

static __init int pid_namespace_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	/* "kernel" directory will have already been initialized. */
	BUG_ON(register_pidns_sysctls(&init_pid_ns));
#endif
	return 0;
}
subsys_initcall(pid_namespace_sysctl_init);

static struct file *__pidfd_fget(struct task_struct *task, int fd)
{
	struct file *file;
	int ret;

	ret = down_read_killable(&task->signal->exec_update_lock);
	if (ret)
		return ERR_PTR(ret);

	if (ptrace_may_access(task, PTRACE_MODE_ATTACH_REALCREDS))
		file = fget_task(task, fd);
	else
		file = ERR_PTR(-EPERM);

	up_read(&task->signal->exec_update_lock);

	if (!file) {
		/*
		 * It is possible that the target thread is exiting; it can be
		 * either:
		 * 1. before exit_signals(), which gives a real fd
		 * 2. before exit_files() takes the task_lock() gives a real fd
		 * 3. after exit_files() releases task_lock(), ->files is NULL;
		 *    this has PF_EXITING, since it was set in exit_signals(),
		 *    __pidfd_fget() returns EBADF.
		 * In case 3 we get EBADF, but that really means ESRCH, since
		 * the task is currently exiting and has freed its files
		 * struct, so we fix it up.
		 */
		if (task->flags & PF_EXITING)
			file = ERR_PTR(-ESRCH);
		else
			file = ERR_PTR(-EBADF);
	}

	return file;
}

static int pidfd_getfd(struct pid *pid, int fd)
{
	struct task_struct *task;
	struct file *file;
	int ret;

	task = get_pid_task(pid, PIDTYPE_PID);
	if (!task)
		return -ESRCH;

	file = __pidfd_fget(task, fd);
	put_task_struct(task);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = receive_fd(file, NULL, O_CLOEXEC);
	fput(file);

	return ret;
}

/**
 * sys_pidfd_getfd() - Get a file descriptor from another process
 *
 * @pidfd:	the pidfd file descriptor of the process
 * @fd:		the file descriptor number to get
 * @flags:	flags on how to get the fd (reserved)
 *
 * This syscall gets a copy of a file descriptor from another process
 * based on the pidfd, and file descriptor number. It requires that
 * the calling process has the ability to ptrace the process represented
 * by the pidfd. The process which is having its file descriptor copied
 * is otherwise unaffected.
 *
 * Return: On success, a cloexec file descriptor is returned.
 *         On error, a negative errno number will be returned.
 */
SYSCALL_DEFINE3(pidfd_getfd, int, pidfd, int, fd,
		unsigned int, flags)
{
	struct pid *pid;

	/* flags is currently unused - make sure it's unset */
	if (flags)
		return -EINVAL;

	CLASS(fd, f)(pidfd);
	if (fd_empty(f))
		return -EBADF;

	pid = pidfd_pid(fd_file(f));
	if (IS_ERR(pid))
		return PTR_ERR(pid);

	return pidfd_getfd(pid, fd);
}
