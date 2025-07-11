// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/sched/mm.h>

#include "internal.h"

/*
 * Logic: we've got two memory sums for each process, "shared", and
 * "non-shared". Shared memory may get counted more than once, for
 * each process that owns it. Non-shared memory is counted
 * accurately.
 */
void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;
	struct vm_region *region;
	unsigned long bytes = 0, sbytes = 0, slack = 0, size;

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		bytes += kobjsize(vma);

		region = vma->vm_region;
		if (region) {
			size = kobjsize(region);
			size += region->vm_end - region->vm_start;
		} else {
			size = vma->vm_end - vma->vm_start;
		}

		if (atomic_read(&mm->mm_count) > 1 ||
		    is_nommu_shared_mapping(vma->vm_flags)) {
			sbytes += size;
		} else {
			bytes += size;
			if (region)
				slack = region->vm_end - vma->vm_end;
		}
	}

	if (atomic_read(&mm->mm_count) > 1)
		sbytes += kobjsize(mm);
	else
		bytes += kobjsize(mm);

	if (current->fs && current->fs->users > 1)
		sbytes += kobjsize(current->fs);
	else
		bytes += kobjsize(current->fs);

	if (current->files && atomic_read(&current->files->count) > 1)
		sbytes += kobjsize(current->files);
	else
		bytes += kobjsize(current->files);

	if (current->sighand && refcount_read(&current->sighand->count) > 1)
		sbytes += kobjsize(current->sighand);
	else
		bytes += kobjsize(current->sighand);

	bytes += kobjsize(current); /* includes kernel stack */

	mmap_read_unlock(mm);

	seq_printf(m,
		"Mem:\t%8lu bytes\n"
		"Slack:\t%8lu bytes\n"
		"Shared:\t%8lu bytes\n",
		bytes, slack, sbytes);
}

unsigned long task_vsize(struct mm_struct *mm)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;
	unsigned long vsize = 0;

	mmap_read_lock(mm);
	for_each_vma(vmi, vma)
		vsize += vma->vm_end - vma->vm_start;
	mmap_read_unlock(mm);
	return vsize;
}

unsigned long task_statm(struct mm_struct *mm,
			 unsigned long *shared, unsigned long *text,
			 unsigned long *data, unsigned long *resident)
{
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;
	struct vm_region *region;
	unsigned long size = kobjsize(mm);

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		size += kobjsize(vma);
		region = vma->vm_region;
		if (region) {
			size += kobjsize(region);
			size += region->vm_end - region->vm_start;
		}
	}

	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
		>> PAGE_SHIFT;
	*data = (PAGE_ALIGN(mm->start_stack) - (mm->start_data & PAGE_MASK))
		>> PAGE_SHIFT;
	mmap_read_unlock(mm);
	size >>= PAGE_SHIFT;
	size += *text + *data;
	*resident = size;
	return size;
}

/*
 * display a single VMA to a sequenced file
 */
static int nommu_vma_show(struct seq_file *m, struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long ino = 0;
	struct file *file;
	dev_t dev = 0;
	int flags;
	unsigned long long pgoff = 0;

	flags = vma->vm_flags;
	file = vma->vm_file;

	if (file) {
		struct inode *inode = file_inode(vma->vm_file);
		dev = inode_get_dev(inode);
		ino = inode->i_ino;
		pgoff = (loff_t)vma->vm_pgoff << PAGE_SHIFT;
	}

	seq_setwidth(m, 25 + sizeof(void *) * 6 - 1);
	seq_printf(m,
		   "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu ",
		   vma->vm_start,
		   vma->vm_end,
		   flags & VM_READ ? 'r' : '-',
		   flags & VM_WRITE ? 'w' : '-',
		   flags & VM_EXEC ? 'x' : '-',
		   flags & VM_MAYSHARE ? flags & VM_SHARED ? 'S' : 's' : 'p',
		   pgoff,
		   MAJOR(dev), MINOR(dev), ino);

	if (file) {
		seq_pad(m, ' ');
		seq_path(m, file_user_path(file), "");
	} else if (mm && vma_is_initial_stack(vma)) {
		seq_pad(m, ' ');
		seq_puts(m, "[stack]");
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display mapping lines for a particular process's /proc/pid/maps
 */
static int show_map(struct seq_file *m, void *_p)
{
	return nommu_vma_show(m, _p);
}

static struct vm_area_struct *proc_get_vma(struct proc_maps_private *priv,
						loff_t *ppos)
{
	struct vm_area_struct *vma = vma_next(&priv->iter);

	if (vma) {
		*ppos = vma->vm_start;
	} else {
		*ppos = -1UL;
	}

	return vma;
}

static void *m_start(struct seq_file *m, loff_t *ppos)
{
	struct proc_maps_private *priv = m->private;
	unsigned long last_addr = *ppos;
	struct mm_struct *mm;

	/* See proc_get_vma(). Zero at the start or after lseek. */
	if (last_addr == -1UL)
		return NULL;

	/* pin the task and mm whilst we play with them */
	priv->task = get_proc_task(priv->inode);
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	mm = priv->mm;
	if (!mm || !mmget_not_zero(mm)) {
		put_task_struct(priv->task);
		priv->task = NULL;
		return NULL;
	}

	if (mmap_read_lock_killable(mm)) {
		mmput(mm);
		put_task_struct(priv->task);
		priv->task = NULL;
		return ERR_PTR(-EINTR);
	}

	vma_iter_init(&priv->iter, mm, last_addr);

	return proc_get_vma(priv, ppos);
}

static void m_stop(struct seq_file *m, void *v)
{
	struct proc_maps_private *priv = m->private;
	struct mm_struct *mm = priv->mm;

	if (!priv->task)
		return;

	mmap_read_unlock(mm);
	mmput(mm);
	put_task_struct(priv->task);
	priv->task = NULL;
}

static void *m_next(struct seq_file *m, void *_p, loff_t *ppos)
{
	return proc_get_vma(m->private, ppos);
}

static const struct seq_operations proc_pid_maps_ops = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

static int maps_open(struct inode *inode, struct file *file,
		     const struct seq_operations *ops)
{
	struct proc_maps_private *priv;

	priv = __seq_open_private(file, ops, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->inode = inode;
	priv->mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR_OR_NULL(priv->mm)) {
		int err = priv->mm ? PTR_ERR(priv->mm) : -ESRCH;

		seq_release_private(inode, file);
		return err;
	}

	return 0;
}


static int map_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	if (priv->mm)
		mmdrop(priv->mm);

	return seq_release_private(inode, file);
}

static int pid_maps_open(struct inode *inode, struct file *file)
{
	return maps_open(inode, file, &proc_pid_maps_ops);
}

const struct file_operations proc_pid_maps_operations = {
	.open		= pid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= map_release,
};

