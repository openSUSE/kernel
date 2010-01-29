/*
 * Implement the manual drop-all-pagecache function
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/sysctl.h>
#include <linux/gfp.h>

/* A global variable is a bit ugly, but it keeps the code simple */
int sysctl_drop_caches;

static void drop_pagecache_sb(struct super_block *sb)
{
	int i;

	for_each_possible_cpu(i) {
		struct inode *inode, *toput_inode = NULL;
		struct list_head *list;
#ifdef CONFIG_SMP
                list = per_cpu_ptr(sb->s_inodes, i);
#else
                list = &sb->s_inodes;
#endif
		rcu_read_lock();
		list_for_each_entry_rcu(inode, list, i_sb_list) {
			spin_lock(&inode->i_lock);
			if (inode->i_state & (I_FREEING|I_CLEAR|I_WILL_FREE|I_NEW)
					|| inode->i_mapping->nrpages == 0) {
				spin_unlock(&inode->i_lock);
				continue;
			}
			__iget(inode);
			spin_unlock(&inode->i_lock);
			rcu_read_unlock();
			invalidate_mapping_pages(inode->i_mapping, 0, -1);
			iput(toput_inode);
			toput_inode = inode;
			rcu_read_lock();
		}
		rcu_read_unlock();
		iput(toput_inode);
	}
}

static void drop_pagecache(void)
{
	struct super_block *sb;

	spin_lock(&sb_lock);
restart:
	list_for_each_entry(sb, &super_blocks, s_list) {
		sb->s_count++;
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (sb->s_root)
			drop_pagecache_sb(sb);
		up_read(&sb->s_umount);
		spin_lock(&sb_lock);
		if (__put_super_and_need_restart(sb))
			goto restart;
	}
	spin_unlock(&sb_lock);
}

static void drop_slab(void)
{
	int nr_objects;

	do {
		nr_objects = shrink_slab(1000, GFP_KERNEL, 1000);
	} while (nr_objects > 10);
}

int drop_caches_sysctl_handler(ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (write) {
		if (sysctl_drop_caches & 1)
			drop_pagecache();
		if (sysctl_drop_caches & 2)
			drop_slab();
	}
	return 0;
}
