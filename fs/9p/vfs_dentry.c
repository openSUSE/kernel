// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contians vfs dentry ops for the 9P2000 protocol.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "fid.h"

/**
 * v9fs_cached_dentry_delete - called when dentry refcount equals 0
 * @dentry:  dentry in question
 *
 */
static int v9fs_cached_dentry_delete(const struct dentry *dentry)
{
	p9_debug(P9_DEBUG_VFS, " dentry: %pd (%p)\n",
		 dentry, dentry);

	/* Don't cache negative dentries */
	if (d_really_is_negative(dentry))
		return 1;
	return 0;
}

/**
 * v9fs_dentry_release - called when dentry is going to be freed
 * @dentry:  dentry that is being release
 *
 */

static void v9fs_dentry_release(struct dentry *dentry)
{
	struct hlist_node *p, *n;
	struct hlist_head head;

	p9_debug(P9_DEBUG_VFS, " dentry: %pd (%p)\n",
		 dentry, dentry);

	spin_lock(&dentry->d_lock);
	hlist_move_list((struct hlist_head *)&dentry->d_fsdata, &head);
	spin_unlock(&dentry->d_lock);

	hlist_for_each_safe(p, n, &head)
		p9_fid_put(hlist_entry(p, struct p9_fid, dlist));
}

static int __v9fs_lookup_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct p9_fid *fid;
	struct inode *inode;
	struct v9fs_inode *v9inode;
	unsigned int cached;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	inode = d_inode(dentry);
	if (!inode)
		goto out_valid;

	v9inode = V9FS_I(inode);
	struct v9fs_session_info *v9ses = v9fs_inode2v9ses(inode);

	cached = v9ses->cache & (CACHE_META | CACHE_LOOSE);

	if (!cached || v9inode->cache_validity & V9FS_INO_INVALID_ATTR) {
		int retval;
		struct v9fs_session_info *v9ses;

		fid = v9fs_fid_lookup(dentry);
		if (IS_ERR(fid)) {
			p9_debug(
				P9_DEBUG_VFS,
				"v9fs_fid_lookup: dentry = %pd (%p), got error %pe\n",
				dentry, dentry, fid);
			return PTR_ERR(fid);
		}

		v9ses = v9fs_inode2v9ses(inode);
		if (v9fs_proto_dotl(v9ses))
			retval = v9fs_refresh_inode_dotl(fid, inode);
		else
			retval = v9fs_refresh_inode(fid, inode);
		p9_fid_put(fid);

		if (retval == -ENOENT) {
			p9_debug(P9_DEBUG_VFS, "dentry: %pd (%p) invalidated due to ENOENT\n",
				 dentry, dentry);
			return 0;
		}
		if (v9inode->cache_validity & V9FS_INO_INVALID_ATTR) {
			p9_debug(P9_DEBUG_VFS, "dentry: %pd (%p) invalidated due to type change\n",
				 dentry, dentry);
			return 0;
		}
		if (retval < 0) {
			p9_debug(P9_DEBUG_VFS,
				"refresh inode: dentry = %pd (%p), got error %pe\n",
				dentry, dentry, ERR_PTR(retval));
			return retval;
		}
	}
out_valid:
	p9_debug(P9_DEBUG_VFS, "dentry: %pd (%p) is valid\n", dentry, dentry);
	return 1;
}

static int v9fs_lookup_revalidate(struct inode *dir, const struct qstr *name,
				  struct dentry *dentry, unsigned int flags)
{
	return __v9fs_lookup_revalidate(dentry, flags);
}

static bool v9fs_dentry_unalias_trylock(const struct dentry *dentry)
{
	struct v9fs_session_info *v9ses = v9fs_dentry2v9ses(dentry);
	return down_write_trylock(&v9ses->rename_sem);
}

static void v9fs_dentry_unalias_unlock(const struct dentry *dentry)
{
	struct v9fs_session_info *v9ses = v9fs_dentry2v9ses(dentry);
	up_write(&v9ses->rename_sem);
}

const struct dentry_operations v9fs_cached_dentry_operations = {
	.d_revalidate = v9fs_lookup_revalidate,
	.d_weak_revalidate = __v9fs_lookup_revalidate,
	.d_delete = v9fs_cached_dentry_delete,
	.d_release = v9fs_dentry_release,
	.d_unalias_trylock = v9fs_dentry_unalias_trylock,
	.d_unalias_unlock = v9fs_dentry_unalias_unlock,
};

const struct dentry_operations v9fs_dentry_operations = {
	.d_revalidate = v9fs_lookup_revalidate,
	.d_weak_revalidate = __v9fs_lookup_revalidate,
	.d_release = v9fs_dentry_release,
	.d_unalias_trylock = v9fs_dentry_unalias_trylock,
	.d_unalias_unlock = v9fs_dentry_unalias_unlock,
};
