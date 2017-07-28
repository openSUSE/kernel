/*
 * Squashfs3 - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@lougher.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * inode.c
 */

#include <linux/module.h>
#include <linux/zlib.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blk_types.h>
#include <linux/vfs.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/exportfs.h>
#include <linux/sched.h>
#include <linux/magic.h>
#include <linux/slab.h>

#include "squashfs3_fs.h"
#include "squashfs3_fs_sb.h"
#include "squashfs3_fs_i.h"
#include "squashfs3.h"

static struct dentry *squashfs3_fh_to_dentry(struct super_block *s,
		struct fid *fid, int fh_len, int fh_type);
static struct dentry *squashfs3_fh_to_parent(struct super_block *s,
		struct fid *fid, int fh_len, int fh_type);
static struct dentry *squashfs3_get_parent(struct dentry *child);
static int squashfs3_read_inode(struct inode *i, squashfs3_inode_t inode);
static int squashfs3_statfs(struct dentry *, struct kstatfs *);
static int squashfs3_symlink_readpage(struct file *file, struct page *page);
static long long read_blocklist(struct inode *inode, int index,
				int readahead_blks, char *block_list,
				unsigned short **block_p, unsigned int *bsize);
static int squashfs3_readpage(struct file *file, struct page *page);
static int squashfs3_readdir(struct file *, struct dir_context *);
static struct dentry *squashfs3_lookup(struct inode *, struct dentry *,
				unsigned int);
static int squashfs3_remount(struct super_block *s, int *flags, char *data);
static void squashfs3_put_super(struct super_block *);
static struct dentry *squashfs3_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *data);
static struct inode *squashfs3_alloc_inode(struct super_block *sb);
static void squashfs3_destroy_inode(struct inode *inode);
static int init_inodecache(void);
static void destroy_inodecache(void);

static struct file_system_type squashfs3_fs_type = {
	.owner = THIS_MODULE,
	.name = "squashfs3",
	.mount = squashfs3_mount,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV
};

static const unsigned char squashfs3_filetype_table[] = {
	DT_UNKNOWN, DT_DIR, DT_REG, DT_LNK, DT_BLK, DT_CHR, DT_FIFO, DT_SOCK
};

static struct super_operations squashfs3_super_ops = {
	.alloc_inode = squashfs3_alloc_inode,
	.destroy_inode = squashfs3_destroy_inode,
	.statfs = squashfs3_statfs,
	.put_super = squashfs3_put_super,
	.remount_fs = squashfs3_remount
};

static struct export_operations squashfs3_export_ops = {
	.fh_to_dentry = squashfs3_fh_to_dentry,
	.fh_to_parent = squashfs3_fh_to_parent,
	.get_parent = squashfs3_get_parent
};

SQSH_EXTERN const struct address_space_operations squashfs3_symlink_aops = {
	.readpage = squashfs3_symlink_readpage
};

SQSH_EXTERN const struct address_space_operations squashfs3_aops = {
	.readpage = squashfs3_readpage
};

static const struct file_operations squashfs3_dir_ops = {
	.read = generic_read_dir,
	.iterate = squashfs3_readdir
};

SQSH_EXTERN struct inode_operations squashfs3_dir_inode_ops = {
	.lookup = squashfs3_lookup
};


static struct buffer_head *get_block_length(struct super_block *s,
				int *cur_index, int *offset, int *c_byte)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	unsigned short temp;
	struct buffer_head *bh;

	if (!(bh = sb_bread(s, *cur_index)))
		goto out;

	if (msblk->devblksize - *offset == 1) {
		if (msblk->swap)
			((unsigned char *) &temp)[1] = *((unsigned char *)
				(bh->b_data + *offset));
		else
			((unsigned char *) &temp)[0] = *((unsigned char *)
				(bh->b_data + *offset));
		brelse(bh);
		if (!(bh = sb_bread(s, ++(*cur_index))))
			goto out;
		if (msblk->swap)
			((unsigned char *) &temp)[0] = *((unsigned char *)
				bh->b_data);
		else
			((unsigned char *) &temp)[1] = *((unsigned char *)
				bh->b_data);
		*c_byte = temp;
		*offset = 1;
	} else {
		if (msblk->swap) {
			((unsigned char *) &temp)[1] = *((unsigned char *)
				(bh->b_data + *offset));
			((unsigned char *) &temp)[0] = *((unsigned char *)
				(bh->b_data + *offset + 1));
		} else {
			((unsigned char *) &temp)[0] = *((unsigned char *)
				(bh->b_data + *offset));
			((unsigned char *) &temp)[1] = *((unsigned char *)
				(bh->b_data + *offset + 1));
		}
		*c_byte = temp;
		*offset += 2;
	}

	if (SQUASHFS3_CHECK_DATA(msblk->sblk.flags)) {
		if (*offset == msblk->devblksize) {
			brelse(bh);
			if (!(bh = sb_bread(s, ++(*cur_index))))
				goto out;
			*offset = 0;
		}
		if (*((unsigned char *) (bh->b_data + *offset)) !=
						SQUASHFS3_MARKER_BYTE) {
			ERROR("Metadata block marker corrupt @ %x\n",
						*cur_index);
			brelse(bh);
			goto out;
		}
		(*offset)++;
	}
	return bh;

out:
	return NULL;
}


SQSH_EXTERN unsigned int squashfs3_read_data(struct super_block *s, char *buffer,
			long long index, unsigned int length,
			long long *next_index, int srclength)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	struct buffer_head **bh;
	unsigned int offset = index & ((1 << msblk->devblksize_log2) - 1);
	unsigned int cur_index = index >> msblk->devblksize_log2;
	int bytes, avail_bytes, b = 0, k = 0;
	unsigned int compressed;
	unsigned int c_byte = length;

	bh = kmalloc(((sblk->block_size >> msblk->devblksize_log2) + 1) *
								sizeof(struct buffer_head *), GFP_KERNEL);
	if (bh == NULL)
		goto read_failure;

	if (c_byte) {
		bytes = -offset;
		compressed = SQUASHFS3_COMPRESSED_BLOCK(c_byte);
		c_byte = SQUASHFS3_COMPRESSED_SIZE_BLOCK(c_byte);

		TRACE("Block @ 0x%llx, %scompressed size %d, src size %d\n", index,
					compressed ? "" : "un", (unsigned int) c_byte, srclength);

		if (c_byte > srclength || index < 0 || (index + c_byte) > sblk->bytes_used)
			goto read_failure;

		for (b = 0; bytes < (int) c_byte; b++, cur_index++) {
			bh[b] = sb_getblk(s, cur_index);
			if (bh[b] == NULL)
				goto block_release;
			bytes += msblk->devblksize;
		}
		ll_rw_block(REQ_OP_READ, 0, b, bh);
	} else {
		if (index < 0 || (index + 2) > sblk->bytes_used)
			goto read_failure;

		bh[0] = get_block_length(s, &cur_index, &offset, &c_byte);
		if (bh[0] == NULL)
			goto read_failure;
		b = 1;

		bytes = msblk->devblksize - offset;
		compressed = SQUASHFS3_COMPRESSED(c_byte);
		c_byte = SQUASHFS3_COMPRESSED_SIZE(c_byte);

		TRACE("Block @ 0x%llx, %scompressed size %d\n", index, compressed
					? "" : "un", (unsigned int) c_byte);

		if (c_byte > srclength || (index + c_byte) > sblk->bytes_used)
			goto block_release;

		for (; bytes < c_byte; b++) {
			bh[b] = sb_getblk(s, ++cur_index);
			if (bh[b] == NULL)
				goto block_release;
			bytes += msblk->devblksize;
		}
		ll_rw_block(REQ_OP_READ, 0, b - 1, bh + 1);
	}

	if (compressed) {
		int zlib_err = 0;

		/*
		 * uncompress block
		 */

		mutex_lock(&msblk->read_data_mutex);

		msblk->stream.next_out = buffer;
		msblk->stream.avail_out = srclength;

		for (bytes = 0; k < b; k++) {
			avail_bytes = min(c_byte - bytes, msblk->devblksize - offset);

			wait_on_buffer(bh[k]);
			if (!buffer_uptodate(bh[k]))
				goto release_mutex;

			msblk->stream.next_in = bh[k]->b_data + offset;
			msblk->stream.avail_in = avail_bytes;

			if (k == 0) {
				zlib_err = zlib_inflateInit(&msblk->stream);
				if (zlib_err != Z_OK) {
					ERROR("zlib_inflateInit returned unexpected result 0x%x,"
						" srclength %d\n", zlib_err, srclength);
					goto release_mutex;
				}

				if (avail_bytes == 0) {
					offset = 0;
					brelse(bh[k]);
					continue;
				}
			}

			zlib_err = zlib_inflate(&msblk->stream, Z_NO_FLUSH);
			if (zlib_err != Z_OK && zlib_err != Z_STREAM_END) {
				ERROR("zlib_inflate returned unexpected result 0x%x,"
					" srclength %d, avail_in %lx, avail_out %lx\n", zlib_err,
					srclength, msblk->stream.avail_in, msblk->stream.avail_out);
				goto release_mutex;
			}

			bytes += avail_bytes;
			offset = 0;
			brelse(bh[k]);
		}

		if (zlib_err != Z_STREAM_END)
			goto release_mutex;

		zlib_err = zlib_inflateEnd(&msblk->stream);
		if (zlib_err != Z_OK) {
			ERROR("zlib_inflateEnd returned unexpected result 0x%x,"
				" srclength %d\n", zlib_err, srclength);
			goto release_mutex;
		}
		bytes = msblk->stream.total_out;
		mutex_unlock(&msblk->read_data_mutex);
	} else {
		int i;

		for(i = 0; i < b; i++) {
			wait_on_buffer(bh[i]);
			if (!buffer_uptodate(bh[i]))
				goto block_release;
		}

		for (bytes = 0; k < b; k++) {
			avail_bytes = min(c_byte - bytes, msblk->devblksize - offset);

			memcpy(buffer + bytes, bh[k]->b_data + offset, avail_bytes);
			bytes += avail_bytes;
			offset = 0;
			brelse(bh[k]);
		}
	}

	if (next_index)
		*next_index = index + c_byte + (length ? 0 :
				(SQUASHFS3_CHECK_DATA(msblk->sblk.flags) ? 3 : 2));

	kfree(bh);
	return bytes;

release_mutex:
	mutex_unlock(&msblk->read_data_mutex);

block_release:
	for (; k < b; k++)
		brelse(bh[k]);

read_failure:
	ERROR("sb_bread failed reading block 0x%x\n", cur_index);
	kfree(bh);
	return 0;
}


static struct squashfs3_cache_entry *squashfs3_cache_get(struct super_block *s,
	struct squashfs3_cache *cache, long long block, int length)
{
	int i, n;
	struct squashfs3_cache_entry *entry;

	spin_lock(&cache->lock);

	while (1) {
		for (i = 0; i < cache->entries && cache->entry[i].block != block; i++);

		if (i == cache->entries) {
			if (cache->unused_blks == 0) {
				cache->waiting ++;
				spin_unlock(&cache->lock);
				wait_event(cache->wait_queue, cache->unused_blks);
				spin_lock(&cache->lock);
				cache->waiting --;
				continue;
			}

			i = cache->next_blk;
			for (n = 0; n < cache->entries; n++) {
				if (cache->entry[i].locked == 0)
					break;
				i = (i + 1) % cache->entries;
			}

			cache->next_blk = (i + 1) % cache->entries;
			entry = &cache->entry[i];

			cache->unused_blks --;
			entry->block = block;
			entry->locked = 1;
			entry->pending = 1;
			entry->waiting = 0;
			entry->error = 0;
			spin_unlock(&cache->lock);

			entry->length = squashfs3_read_data(s, entry->data,
				block, length, &entry->next_index, cache->block_size);

			spin_lock(&cache->lock);

			if (entry->length == 0)
				entry->error = 1;

			entry->pending = 0;
			spin_unlock(&cache->lock);
			if (entry->waiting)
				wake_up_all(&entry->wait_queue);
			goto out;
		}

		entry = &cache->entry[i];
		if (entry->locked == 0)
			cache->unused_blks --;
		entry->locked++;

		if (entry->pending) {
			entry->waiting ++;
			spin_unlock(&cache->lock);
			wait_event(entry->wait_queue, !entry->pending);
			goto out;
		}

		spin_unlock(&cache->lock);
		goto out;
	}

out:
	TRACE("Got %s %d, start block %lld, locked %d, error %d\n", i,
		cache->name, entry->block, entry->locked, entry->error);
	if (entry->error)
		ERROR("Unable to read %s cache entry [%llx]\n", cache->name, block);
	return entry;
}


static void squashfs3_cache_put(struct squashfs3_cache *cache,
				struct squashfs3_cache_entry *entry)
{
	spin_lock(&cache->lock);
	entry->locked --;
	if (entry->locked == 0) {
		cache->unused_blks ++;
		spin_unlock(&cache->lock);
		if (cache->waiting)
			wake_up(&cache->wait_queue);
	} else
		spin_unlock(&cache->lock);
}


static void squashfs3_cache_delete(struct squashfs3_cache *cache)
{
	int i;

	if (cache == NULL)
		return;

	for (i = 0; i < cache->entries; i++)
		if (cache->entry[i].data) {
			if (cache->use_vmalloc)
				vfree(cache->entry[i].data);
			else
				kfree(cache->entry[i].data);
		}

	kfree(cache);
}


static struct squashfs3_cache *squashfs3_cache_init(char *name, int entries,
	int block_size, int use_vmalloc)
{
	int i;
	struct squashfs3_cache *cache = kzalloc(sizeof(struct squashfs3_cache) +
			entries * sizeof(struct squashfs3_cache_entry), GFP_KERNEL);
	if (cache == NULL) {
		ERROR("Failed to allocate %s cache\n", name);
		goto failed;
	}

	cache->next_blk = 0;
	cache->unused_blks = entries;
	cache->entries = entries;
	cache->block_size = block_size;
	cache->use_vmalloc = use_vmalloc;
	cache->name = name;
	cache->waiting = 0;
	spin_lock_init(&cache->lock);
	init_waitqueue_head(&cache->wait_queue);

	for (i = 0; i < entries; i++) {
		init_waitqueue_head(&cache->entry[i].wait_queue);
		cache->entry[i].block = SQUASHFS3_INVALID_BLK;
		cache->entry[i].data = use_vmalloc ? vmalloc(block_size) :
				kmalloc(block_size, GFP_KERNEL);
		if (cache->entry[i].data == NULL) {
			ERROR("Failed to allocate %s cache entry\n", name);
			goto cleanup;
		}
	}

	return cache;

cleanup:
	squashfs3_cache_delete(cache);
failed:
	return NULL;
}


SQSH_EXTERN int squashfs3_get_cached_block(struct super_block *s, void *buffer,
				long long block, unsigned int offset,
				int length, long long *next_block,
				unsigned int *next_offset)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	int bytes, return_length = length;
	struct squashfs3_cache_entry *entry;

	TRACE("Entered squashfs3_get_cached_block [%llx:%x]\n", block, offset);

	while (1) {
		entry = squashfs3_cache_get(s, msblk->block_cache, block, 0);
		bytes = entry->length - offset;

		if (entry->error || bytes < 1) {
			return_length = 0;
			goto finish;
		} else if (bytes >= length) {
			if (buffer)
				memcpy(buffer, entry->data + offset, length);
			if (entry->length - offset == length) {
				*next_block = entry->next_index;
				*next_offset = 0;
			} else {
				*next_block = block;
				*next_offset = offset + length;
			}
			goto finish;
		} else {
			if (buffer) {
				memcpy(buffer, entry->data + offset, bytes);
				buffer = (char *) buffer + bytes;
			}
			block = entry->next_index;
			squashfs3_cache_put(msblk->block_cache, entry);
			length -= bytes;
			offset = 0;
		}
	}

finish:
	squashfs3_cache_put(msblk->block_cache, entry);
	return return_length;
}


static int get_fragment_location(struct super_block *s, unsigned int fragment,
				long long *fragment_start_block,
				unsigned int *fragment_size)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	long long start_block =
		msblk->fragment_index[SQUASHFS3_FRAGMENT_INDEX(fragment)];
	int offset = SQUASHFS3_FRAGMENT_INDEX_OFFSET(fragment);
	struct squashfs3_fragment_entry fragment_entry;

	if (msblk->swap) {
		struct squashfs3_fragment_entry sfragment_entry;

		if (!squashfs3_get_cached_block(s, &sfragment_entry, start_block, offset,
					 sizeof(sfragment_entry), &start_block, &offset))
			goto out;
		SQUASHFS3_SWAP_FRAGMENT_ENTRY(&fragment_entry, &sfragment_entry);
	} else
		if (!squashfs3_get_cached_block(s, &fragment_entry, start_block, offset,
					 sizeof(fragment_entry), &start_block, &offset))
			goto out;

	*fragment_start_block = fragment_entry.start_block;
	*fragment_size = fragment_entry.size;

	return 1;

out:
	return 0;
}


static void release_cached_fragment(struct squashfs3_sb_info *msblk,
				struct squashfs3_cache_entry *fragment)
{
	squashfs3_cache_put(msblk->fragment_cache, fragment);
}


static struct squashfs3_cache_entry *get_cached_fragment(struct super_block *s,
				long long start_block, int length)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;

	return squashfs3_cache_get(s, msblk->fragment_cache, start_block, length);
}


static void squashfs3_new_inode(struct squashfs3_sb_info *msblk, struct inode *i,
				struct squashfs3_base_inode_header *inodeb)
{
	i->i_ino = inodeb->inode_number;
	i->i_mtime.tv_sec = inodeb->mtime;
	i->i_atime.tv_sec = inodeb->mtime;
	i->i_ctime.tv_sec = inodeb->mtime;
	i->i_uid = make_kuid(&init_user_ns, msblk->uid[inodeb->uid]);
	i->i_mode = inodeb->mode;
	i->i_size = 0;

	if (inodeb->guid == SQUASHFS3_GUIDS)
		i->i_gid = make_kgid(&init_user_ns, msblk->uid[inodeb->uid]);
	else
		i->i_gid = make_kgid(&init_user_ns, msblk->guid[inodeb->guid]);
}


static squashfs3_inode_t squashfs3_inode_lookup(struct super_block *s, int ino)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	long long start = msblk->inode_lookup_table[SQUASHFS3_LOOKUP_BLOCK(ino - 1)];
	int offset = SQUASHFS3_LOOKUP_BLOCK_OFFSET(ino - 1);
	squashfs3_inode_t inode;

	TRACE("Entered squashfs3_inode_lookup, inode_number = %d\n", ino);

	if (msblk->swap) {
		squashfs3_inode_t sinode;

		if (!squashfs3_get_cached_block(s, &sinode, start, offset,
					sizeof(sinode), &start, &offset))
			goto out;
		SQUASHFS3_SWAP_INODE_T((&inode), &sinode);
	} else if (!squashfs3_get_cached_block(s, &inode, start, offset,
					sizeof(inode), &start, &offset))
			goto out;

	TRACE("squashfs3_inode_lookup, inode = 0x%llx\n", inode);

	return inode;

out:
	return SQUASHFS3_INVALID_BLK;
}



static struct dentry *squashfs3_export_iget(struct super_block *s,
	unsigned int inode_number)
{
	squashfs3_inode_t inode;
	struct dentry *dentry;

	TRACE("Entered squashfs3_export_iget\n");

	inode = squashfs3_inode_lookup(s, inode_number);
	if(inode == SQUASHFS3_INVALID_BLK) {
		dentry = ERR_PTR(-ENOENT);
		goto failure;
	}

	dentry = d_obtain_alias(squashfs3_iget(s, inode, inode_number));

failure:
	return dentry;
}


static struct dentry *squashfs3_fh_to_dentry(struct super_block *s,
		struct fid *fid, int fh_len, int fh_type)
{
	if((fh_type != FILEID_INO32_GEN && fh_type != FILEID_INO32_GEN_PARENT) ||
			fh_len < 2)
		return NULL;

	return squashfs3_export_iget(s, fid->i32.ino);
}


static struct dentry *squashfs3_fh_to_parent(struct super_block *s,
		struct fid *fid, int fh_len, int fh_type)
{
	if(fh_type != FILEID_INO32_GEN_PARENT || fh_len < 4)
		return NULL;

	return squashfs3_export_iget(s, fid->i32.parent_ino);
}


static struct dentry *squashfs3_get_parent(struct dentry *child)
{
	struct inode *i = child->d_inode;

	TRACE("Entered squashfs3_get_parent\n");

	return squashfs3_export_iget(i->i_sb, SQUASHFS3_I(i)->u.s2.parent_inode);
}


SQSH_EXTERN struct inode *squashfs3_iget(struct super_block *s,
				squashfs3_inode_t inode, unsigned int inode_number)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct inode *i = iget_locked(s, inode_number);

	TRACE("Entered squashfs3_iget\n");

	if(i && (i->i_state & I_NEW)) {
		(msblk->read_inode)(i, inode);
		unlock_new_inode(i);
	}

	return i;
}


static int squashfs3_read_inode(struct inode *i, squashfs3_inode_t inode)
{
	struct super_block *s = i->i_sb;
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	long long block = SQUASHFS3_INODE_BLK(inode) + sblk->inode_table_start;
	unsigned int offset = SQUASHFS3_INODE_OFFSET(inode);
	long long next_block;
	unsigned int next_offset;
	union squashfs3_inode_header id, sid;
	struct squashfs3_base_inode_header *inodeb = &id.base, *sinodeb = &sid.base;

	TRACE("Entered squashfs3_read_inode\n");

	if (msblk->swap) {
		if (!squashfs3_get_cached_block(s, sinodeb, block, offset,
					sizeof(*sinodeb), &next_block, &next_offset))
			goto failed_read;
		SQUASHFS3_SWAP_BASE_INODE_HEADER(inodeb, sinodeb, sizeof(*sinodeb));
	} else
		if (!squashfs3_get_cached_block(s, inodeb, block, offset,
					sizeof(*inodeb), &next_block, &next_offset))
			goto failed_read;

	squashfs3_new_inode(msblk, i, inodeb);

	switch(inodeb->inode_type) {
		case SQUASHFS3_FILE_TYPE: {
			unsigned int frag_size;
			long long frag_blk;
			struct squashfs3_reg_inode_header *inodep = &id.reg;
			struct squashfs3_reg_inode_header *sinodep = &sid.reg;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_REG_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			frag_blk = SQUASHFS3_INVALID_BLK;

			if (inodep->fragment != SQUASHFS3_INVALID_FRAG)
					if(!get_fragment_location(s, inodep->fragment, &frag_blk,
												&frag_size))
						goto failed_read;

			set_nlink(i, 1);
			i->i_size = inodep->file_size;
			i->i_fop = &generic_ro_fops;
			i->i_mode |= S_IFREG;
			i->i_blocks = ((i->i_size - 1) >> 9) + 1;
			SQUASHFS3_I(i)->u.s1.fragment_start_block = frag_blk;
			SQUASHFS3_I(i)->u.s1.fragment_size = frag_size;
			SQUASHFS3_I(i)->u.s1.fragment_offset = inodep->offset;
			SQUASHFS3_I(i)->start_block = inodep->start_block;
			SQUASHFS3_I(i)->u.s1.block_list_start = next_block;
			SQUASHFS3_I(i)->offset = next_offset;
			i->i_data.a_ops = &squashfs3_aops;

			TRACE("File inode %x:%x, start_block %llx, "
					"block_list_start %llx, offset %x\n",
					SQUASHFS3_INODE_BLK(inode), offset,
					inodep->start_block, next_block,
					next_offset);
			break;
		}
		case SQUASHFS3_LREG_TYPE: {
			unsigned int frag_size;
			long long frag_blk;
			struct squashfs3_lreg_inode_header *inodep = &id.lreg;
			struct squashfs3_lreg_inode_header *sinodep = &sid.lreg;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_LREG_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			frag_blk = SQUASHFS3_INVALID_BLK;

			if (inodep->fragment != SQUASHFS3_INVALID_FRAG)
				if (!get_fragment_location(s, inodep->fragment, &frag_blk,
												 &frag_size))
					goto failed_read;

			set_nlink(i, inodep->nlink);
			i->i_size = inodep->file_size;
			i->i_fop = &generic_ro_fops;
			i->i_mode |= S_IFREG;
			i->i_blocks = ((i->i_size - 1) >> 9) + 1;
			SQUASHFS3_I(i)->u.s1.fragment_start_block = frag_blk;
			SQUASHFS3_I(i)->u.s1.fragment_size = frag_size;
			SQUASHFS3_I(i)->u.s1.fragment_offset = inodep->offset;
			SQUASHFS3_I(i)->start_block = inodep->start_block;
			SQUASHFS3_I(i)->u.s1.block_list_start = next_block;
			SQUASHFS3_I(i)->offset = next_offset;
			i->i_data.a_ops = &squashfs3_aops;

			TRACE("File inode %x:%x, start_block %llx, "
					"block_list_start %llx, offset %x\n",
					SQUASHFS3_INODE_BLK(inode), offset,
					inodep->start_block, next_block,
					next_offset);
			break;
		}
		case SQUASHFS3_DIR_TYPE: {
			struct squashfs3_dir_inode_header *inodep = &id.dir;
			struct squashfs3_dir_inode_header *sinodep = &sid.dir;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_DIR_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			set_nlink(i, inodep->nlink);
			i->i_size = inodep->file_size;
			i->i_op = &squashfs3_dir_inode_ops;
			i->i_fop = &squashfs3_dir_ops;
			i->i_mode |= S_IFDIR;
			SQUASHFS3_I(i)->start_block = inodep->start_block;
			SQUASHFS3_I(i)->offset = inodep->offset;
			SQUASHFS3_I(i)->u.s2.directory_index_count = 0;
			SQUASHFS3_I(i)->u.s2.parent_inode = inodep->parent_inode;

			TRACE("Directory inode %x:%x, start_block %x, offset "
					"%x\n", SQUASHFS3_INODE_BLK(inode),
					offset, inodep->start_block,
					inodep->offset);
			break;
		}
		case SQUASHFS3_LDIR_TYPE: {
			struct squashfs3_ldir_inode_header *inodep = &id.ldir;
			struct squashfs3_ldir_inode_header *sinodep = &sid.ldir;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_LDIR_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			set_nlink(i, inodep->nlink);
			i->i_size = inodep->file_size;
			i->i_op = &squashfs3_dir_inode_ops;
			i->i_fop = &squashfs3_dir_ops;
			i->i_mode |= S_IFDIR;
			SQUASHFS3_I(i)->start_block = inodep->start_block;
			SQUASHFS3_I(i)->offset = inodep->offset;
			SQUASHFS3_I(i)->u.s2.directory_index_start = next_block;
			SQUASHFS3_I(i)->u.s2.directory_index_offset = next_offset;
			SQUASHFS3_I(i)->u.s2.directory_index_count = inodep->i_count;
			SQUASHFS3_I(i)->u.s2.parent_inode = inodep->parent_inode;

			TRACE("Long directory inode %x:%x, start_block %x, offset %x\n",
					SQUASHFS3_INODE_BLK(inode), offset,
					inodep->start_block, inodep->offset);
			break;
		}
		case SQUASHFS3_SYMLINK_TYPE: {
			struct squashfs3_symlink_inode_header *inodep = &id.symlink;
			struct squashfs3_symlink_inode_header *sinodep = &sid.symlink;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_SYMLINK_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			set_nlink(i, inodep->nlink);
			i->i_size = inodep->symlink_size;
			i->i_op = &page_symlink_inode_operations;
			i->i_data.a_ops = &squashfs3_symlink_aops;
			i->i_mode |= S_IFLNK;
			SQUASHFS3_I(i)->start_block = next_block;
			SQUASHFS3_I(i)->offset = next_offset;

			TRACE("Symbolic link inode %x:%x, start_block %llx, offset %x\n",
					SQUASHFS3_INODE_BLK(inode), offset,
					next_block, next_offset);
			break;
		 }
		 case SQUASHFS3_BLKDEV_TYPE:
		 case SQUASHFS3_CHRDEV_TYPE: {
			struct squashfs3_dev_inode_header *inodep = &id.dev;
			struct squashfs3_dev_inode_header *sinodep = &sid.dev;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_DEV_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			set_nlink(i, inodep->nlink);
			i->i_mode |= (inodeb->inode_type == SQUASHFS3_CHRDEV_TYPE) ?
					S_IFCHR : S_IFBLK;
			init_special_inode(i, i->i_mode, old_decode_dev(inodep->rdev));

			TRACE("Device inode %x:%x, rdev %x\n",
					SQUASHFS3_INODE_BLK(inode), offset, inodep->rdev);
			break;
		 }
		 case SQUASHFS3_FIFO_TYPE:
		 case SQUASHFS3_SOCKET_TYPE: {
			struct squashfs3_ipc_inode_header *inodep = &id.ipc;
			struct squashfs3_ipc_inode_header *sinodep = &sid.ipc;

			if (msblk->swap) {
				if (!squashfs3_get_cached_block(s, sinodep, block, offset,
						sizeof(*sinodep), &next_block, &next_offset))
					goto failed_read;
				SQUASHFS3_SWAP_IPC_INODE_HEADER(inodep, sinodep);
			} else
				if (!squashfs3_get_cached_block(s, inodep, block, offset,
						sizeof(*inodep), &next_block, &next_offset))
					goto failed_read;

			set_nlink(i, inodep->nlink);
			i->i_mode |= (inodeb->inode_type == SQUASHFS3_FIFO_TYPE)
							? S_IFIFO : S_IFSOCK;
			init_special_inode(i, i->i_mode, 0);
			break;
		 }
		 default:
			ERROR("Unknown inode type %d in squashfs3_iget!\n",
					inodeb->inode_type);
			goto failed_read1;
	}

	return 1;

failed_read:
	ERROR("Unable to read inode [%llx:%x]\n", block, offset);

failed_read1:
	make_bad_inode(i);
	return 0;
}


static int read_inode_lookup_table(struct super_block *s)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	unsigned int length = SQUASHFS3_LOOKUP_BLOCK_BYTES(sblk->inodes);

	TRACE("In read_inode_lookup_table, length %d\n", length);

	/* Allocate inode lookup table */
	msblk->inode_lookup_table = kmalloc(length, GFP_KERNEL);
	if (msblk->inode_lookup_table == NULL) {
		ERROR("Failed to allocate inode lookup table\n");
		return 0;
	}

	if (!squashfs3_read_data(s, (char *) msblk->inode_lookup_table,
			sblk->lookup_table_start, length |
			SQUASHFS3_COMPRESSED_BIT_BLOCK, NULL, length)) {
		ERROR("unable to read inode lookup table\n");
		return 0;
	}

	if (msblk->swap) {
		int i;
		long long block;

		for (i = 0; i < SQUASHFS3_LOOKUP_BLOCKS(sblk->inodes); i++) {
			/* XXX */
			SQUASHFS3_SWAP_LOOKUP_BLOCKS((&block),
						&msblk->inode_lookup_table[i], 1);
			msblk->inode_lookup_table[i] = block;
		}
	}

	return 1;
}


static int read_fragment_index_table(struct super_block *s)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	unsigned int length = SQUASHFS3_FRAGMENT_INDEX_BYTES(sblk->fragments);

	if(length == 0)
		return 1;

	/* Allocate fragment index table */
	msblk->fragment_index = kmalloc(length, GFP_KERNEL);
	if (msblk->fragment_index == NULL) {
		ERROR("Failed to allocate fragment index table\n");
		return 0;
	}

	if (!squashfs3_read_data(s, (char *) msblk->fragment_index,
			sblk->fragment_table_start, length |
			SQUASHFS3_COMPRESSED_BIT_BLOCK, NULL, length)) {
		ERROR("unable to read fragment index table\n");
		return 0;
	}

	if (msblk->swap) {
		int i;
		long long fragment;

		for (i = 0; i < SQUASHFS3_FRAGMENT_INDEXES(sblk->fragments); i++) {
			/* XXX */
			SQUASHFS3_SWAP_FRAGMENT_INDEXES((&fragment),
						&msblk->fragment_index[i], 1);
			msblk->fragment_index[i] = fragment;
		}
	}

	return 1;
}


static int supported_squashfs3_filesystem(struct squashfs3_sb_info *msblk, int silent)
{
	struct squashfs3_super_block *sblk = &msblk->sblk;

	msblk->read_inode = squashfs3_read_inode;
	msblk->read_blocklist = read_blocklist;
	msblk->read_fragment_index_table = read_fragment_index_table;

	if (sblk->s_major == 1) {
		if (!squashfs3_1_0_supported(msblk)) {
			SERROR("Major/Minor mismatch, Squashfs 1.0 filesystems "
				"are unsupported\n");
			SERROR("Please recompile with Squashfs 1.0 support enabled\n");
			return 0;
		}
	} else if (sblk->s_major == 2) {
		if (!squashfs3_2_0_supported(msblk)) {
			SERROR("Major/Minor mismatch, Squashfs 2.0 filesystems "
				"are unsupported\n");
			SERROR("Please recompile with Squashfs 2.0 support enabled\n");
			return 0;
		}
	} else if(sblk->s_major != SQUASHFS3_MAJOR || sblk->s_minor >
			SQUASHFS3_MINOR) {
		SERROR("Major/Minor mismatch, trying to mount newer %d.%d "
				"filesystem\n", sblk->s_major, sblk->s_minor);
		SERROR("Please update your kernel\n");
		return 0;
	}

	return 1;
}


static int squashfs3_fill_super(struct super_block *s, void *data, int silent)
{
	struct squashfs3_sb_info *msblk;
	struct squashfs3_super_block *sblk;
	char b[BDEVNAME_SIZE];
	struct inode *root;

	TRACE("Entered squashfs3_fill_superblock\n");

	s->s_fs_info = kzalloc(sizeof(struct squashfs3_sb_info), GFP_KERNEL);
	if (s->s_fs_info == NULL) {
		ERROR("Failed to allocate superblock\n");
		goto failure;
	}
	msblk = s->s_fs_info;

	msblk->stream.workspace = vmalloc(zlib_inflate_workspacesize());
	if (msblk->stream.workspace == NULL) {
		ERROR("Failed to allocate zlib workspace\n");
		goto failure;
	}
	sblk = &msblk->sblk;

	msblk->devblksize = sb_min_blocksize(s, BLOCK_SIZE);
	msblk->devblksize_log2 = ffz(~msblk->devblksize);

	mutex_init(&msblk->read_data_mutex);
	mutex_init(&msblk->read_page_mutex);
	mutex_init(&msblk->meta_index_mutex);

	/* sblk->bytes_used is checked in squashfs3_read_data to ensure reads are not
	 * beyond filesystem end.  As we're using squashfs3_read_data to read sblk here,
	 * first set sblk->bytes_used to a useful value */
	sblk->bytes_used = sizeof(struct squashfs3_super_block);
	if (!squashfs3_read_data(s, (char *) sblk, SQUASHFS3_START,
					sizeof(struct squashfs3_super_block) |
					SQUASHFS3_COMPRESSED_BIT_BLOCK, NULL, sizeof(struct squashfs3_super_block))) {
		SERROR("unable to read superblock\n");
		goto failed_mount;
	}

	/* Check it is a SQUASHFS3 superblock */
	if ((s->s_magic = sblk->s_magic) != SQUASHFS_MAGIC) {
		if (sblk->s_magic == SQUASHFS3_MAGIC_SWAP) {
			struct squashfs3_super_block ssblk;

			WARNING("Mounting a different endian SQUASHFS3 filesystem on %s\n",
				bdevname(s->s_bdev, b));

			SQUASHFS3_SWAP_SUPER_BLOCK(&ssblk, sblk);
			memcpy(sblk, &ssblk, sizeof(struct squashfs3_super_block));
			msblk->swap = 1;
		} else  {
			if (!silent)
				SERROR("Can't find a SQUASHFS3 superblock on %s\n",
				       bdevname(s->s_bdev, b));
			goto failed_mount;
		}
	}

	/* Check the MAJOR & MINOR versions */
	if(!supported_squashfs3_filesystem(msblk, silent))
		goto failed_mount;

	/* Check the filesystem does not extend beyond the end of the
	   block device */
	if(sblk->bytes_used < 0 || sblk->bytes_used > i_size_read(s->s_bdev->bd_inode))
		goto failed_mount;

	/* Check the root inode for sanity */
	if (SQUASHFS3_INODE_OFFSET(sblk->root_inode) > SQUASHFS3_METADATA_SIZE)
		goto failed_mount;

	TRACE("Found valid superblock on %s\n", bdevname(s->s_bdev, b));
	TRACE("Inodes are %scompressed\n", SQUASHFS3_UNCOMPRESSED_INODES(sblk->flags)
					? "un" : "");
	TRACE("Data is %scompressed\n", SQUASHFS3_UNCOMPRESSED_DATA(sblk->flags)
					? "un" : "");
	TRACE("Check data is %spresent in the filesystem\n",
					SQUASHFS3_CHECK_DATA(sblk->flags) ?  "" : "not ");
	TRACE("Filesystem size %lld bytes\n", sblk->bytes_used);
	TRACE("Block size %d\n", sblk->block_size);
	TRACE("Number of inodes %d\n", sblk->inodes);
	if (sblk->s_major > 1)
		TRACE("Number of fragments %d\n", sblk->fragments);
	TRACE("Number of uids %d\n", sblk->no_uids);
	TRACE("Number of gids %d\n", sblk->no_guids);
	TRACE("sblk->inode_table_start %llx\n", sblk->inode_table_start);
	TRACE("sblk->directory_table_start %llx\n", sblk->directory_table_start);
	if (sblk->s_major > 1)
		TRACE("sblk->fragment_table_start %llx\n", sblk->fragment_table_start);
	TRACE("sblk->uid_start %llx\n", sblk->uid_start);

	s->s_maxbytes = MAX_LFS_FILESIZE;
	s->s_flags |= MS_RDONLY;
	s->s_op = &squashfs3_super_ops;

	msblk->block_cache = squashfs3_cache_init("metadata", SQUASHFS3_CACHED_BLKS,
		SQUASHFS3_METADATA_SIZE, 0);
	if (msblk->block_cache == NULL)
		goto failed_mount;

	/* Allocate read_page block */
	msblk->read_page = vmalloc(sblk->block_size);
	if (msblk->read_page == NULL) {
		ERROR("Failed to allocate read_page block\n");
		goto failed_mount;
	}

	/* Allocate uid and gid tables */
	msblk->uid = kmalloc((sblk->no_uids + sblk->no_guids) *
					sizeof(unsigned int), GFP_KERNEL);
	if (msblk->uid == NULL) {
		ERROR("Failed to allocate uid/gid table\n");
		goto failed_mount;
	}
	msblk->guid = msblk->uid + sblk->no_uids;

	if (msblk->swap) {
		unsigned int suid[sblk->no_uids + sblk->no_guids];

		if (!squashfs3_read_data(s, (char *) &suid, sblk->uid_start,
					((sblk->no_uids + sblk->no_guids) *
					 sizeof(unsigned int)) |
					SQUASHFS3_COMPRESSED_BIT_BLOCK, NULL, (sblk->no_uids + sblk->no_guids) * sizeof(unsigned int))) {
			ERROR("unable to read uid/gid table\n");
			goto failed_mount;
		}

		SQUASHFS3_SWAP_DATA(msblk->uid, suid, (sblk->no_uids +
			sblk->no_guids), (sizeof(unsigned int) * 8));
	} else
		if (!squashfs3_read_data(s, (char *) msblk->uid, sblk->uid_start,
					((sblk->no_uids + sblk->no_guids) *
					 sizeof(unsigned int)) |
					SQUASHFS3_COMPRESSED_BIT_BLOCK, NULL, (sblk->no_uids + sblk->no_guids) * sizeof(unsigned int))) {
			ERROR("unable to read uid/gid table\n");
			goto failed_mount;
		}


	if (sblk->s_major == 1 && squashfs3_1_0_supported(msblk))
		goto allocate_root;

	msblk->fragment_cache = squashfs3_cache_init("fragment",
		SQUASHFS3_CACHED_FRAGMENTS, sblk->block_size, 1);
	if (msblk->fragment_cache == NULL)
		goto failed_mount;

	/* Allocate and read fragment index table */
	if (msblk->read_fragment_index_table(s) == 0)
		goto failed_mount;

	if(sblk->s_major < 3 || sblk->lookup_table_start == SQUASHFS3_INVALID_BLK)
		goto allocate_root;

	/* Allocate and read inode lookup table */
	if (read_inode_lookup_table(s) == 0)
		goto failed_mount;

	s->s_export_op = &squashfs3_export_ops;

allocate_root:
	root = new_inode(s);
	if ((msblk->read_inode)(root, sblk->root_inode) == 0)
		goto failed_mount;
	insert_inode_hash(root);

	s->s_root = d_make_root(root);
	if (s->s_root == NULL) {
		ERROR("Root inode create failed\n");
		goto failed_mount;
	}

	TRACE("Leaving squashfs3_fill_super\n");
	return 0;

failed_mount:
	kfree(msblk->inode_lookup_table);
	kfree(msblk->fragment_index);
	squashfs3_cache_delete(msblk->fragment_cache);
	kfree(msblk->uid);
	vfree(msblk->read_page);
	squashfs3_cache_delete(msblk->block_cache);
	kfree(msblk->fragment_index_2);
	vfree(msblk->stream.workspace);
	kfree(s->s_fs_info);
	s->s_fs_info = NULL;
	return -EINVAL;

failure:
	return -ENOMEM;
}


static int squashfs3_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct squashfs3_sb_info *msblk = dentry->d_sb->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;

	TRACE("Entered squashfs3_statfs\n");

	buf->f_type = SQUASHFS_MAGIC;
	buf->f_bsize = sblk->block_size;
	buf->f_blocks = ((sblk->bytes_used - 1) >> sblk->block_log) + 1;
	buf->f_bfree = buf->f_bavail = 0;
	buf->f_files = sblk->inodes;
	buf->f_ffree = 0;
	buf->f_namelen = SQUASHFS3_NAME_LEN;

	return 0;
}


static int squashfs3_symlink_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	int index = page->index << PAGE_SHIFT, length, bytes, avail_bytes;
	long long block = SQUASHFS3_I(inode)->start_block;
	int offset = SQUASHFS3_I(inode)->offset;
	void *pageaddr = kmap(page);

	TRACE("Entered squashfs3_symlink_readpage, page index %ld, start block "
				"%llx, offset %x\n", page->index,
				SQUASHFS3_I(inode)->start_block,
				SQUASHFS3_I(inode)->offset);

	for (length = 0; length < index; length += bytes) {
		bytes = squashfs3_get_cached_block(inode->i_sb, NULL, block,
				offset, PAGE_SIZE, &block, &offset);
		if (bytes == 0) {
			ERROR("Unable to read symbolic link [%llx:%x]\n", block, offset);
			goto skip_read;
		}
	}

	if (length != index) {
		ERROR("(squashfs3_symlink_readpage) length != index\n");
		bytes = 0;
		goto skip_read;
	}

	avail_bytes = min_t(int, i_size_read(inode) - length, PAGE_SIZE);

	bytes = squashfs3_get_cached_block(inode->i_sb, pageaddr, block, offset,
		avail_bytes, &block, &offset);
	if (bytes == 0)
		ERROR("Unable to read symbolic link [%llx:%x]\n", block, offset);

skip_read:
	memset(pageaddr + bytes, 0, PAGE_SIZE - bytes);
	kunmap(page);
	flush_dcache_page(page);
	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}


static struct squashfs3_meta_index *locate_meta_index(struct inode *inode, int index, int offset)
{
	struct squashfs3_meta_index *meta = NULL;
	struct squashfs3_sb_info *msblk = inode->i_sb->s_fs_info;
	int i;

	mutex_lock(&msblk->meta_index_mutex);

	TRACE("locate_meta_index: index %d, offset %d\n", index, offset);

	if (msblk->meta_index == NULL)
		goto not_allocated;

	for (i = 0; i < SQUASHFS3_META_NUMBER; i ++) {
		if (msblk->meta_index[i].inode_number == inode->i_ino &&
				msblk->meta_index[i].offset >= offset &&
				msblk->meta_index[i].offset <= index &&
				msblk->meta_index[i].locked == 0) {
			TRACE("locate_meta_index: entry %d, offset %d\n", i,
					msblk->meta_index[i].offset);
			meta = &msblk->meta_index[i];
			offset = meta->offset;
		}
	}

	if (meta)
		meta->locked = 1;

not_allocated:
	mutex_unlock(&msblk->meta_index_mutex);

	return meta;
}


static struct squashfs3_meta_index *empty_meta_index(struct inode *inode, int offset, int skip)
{
	struct squashfs3_sb_info *msblk = inode->i_sb->s_fs_info;
	struct squashfs3_meta_index *meta = NULL;
	int i;

	mutex_lock(&msblk->meta_index_mutex);

	TRACE("empty_meta_index: offset %d, skip %d\n", offset, skip);

	if (msblk->meta_index == NULL) {
		msblk->meta_index = kmalloc(sizeof(struct squashfs3_meta_index) *
					SQUASHFS3_META_NUMBER, GFP_KERNEL);
		if (msblk->meta_index == NULL) {
			ERROR("Failed to allocate meta_index\n");
			goto failed;
		}
		for (i = 0; i < SQUASHFS3_META_NUMBER; i++) {
			msblk->meta_index[i].inode_number = 0;
			msblk->meta_index[i].locked = 0;
		}
		msblk->next_meta_index = 0;
	}

	for (i = SQUASHFS3_META_NUMBER; i &&
			msblk->meta_index[msblk->next_meta_index].locked; i --)
		msblk->next_meta_index = (msblk->next_meta_index + 1) %
			SQUASHFS3_META_NUMBER;

	if (i == 0) {
		TRACE("empty_meta_index: failed!\n");
		goto failed;
	}

	TRACE("empty_meta_index: returned meta entry %d, %p\n",
			msblk->next_meta_index,
			&msblk->meta_index[msblk->next_meta_index]);

	meta = &msblk->meta_index[msblk->next_meta_index];
	msblk->next_meta_index = (msblk->next_meta_index + 1) %
			SQUASHFS3_META_NUMBER;

	meta->inode_number = inode->i_ino;
	meta->offset = offset;
	meta->skip = skip;
	meta->entries = 0;
	meta->locked = 1;

failed:
	mutex_unlock(&msblk->meta_index_mutex);
	return meta;
}


static void release_meta_index(struct inode *inode, struct squashfs3_meta_index *meta)
{
	meta->locked = 0;
	smp_mb();
}


static int read_block_index(struct super_block *s, int blocks, char *block_list,
				long long *start_block, int *offset)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	unsigned int *block_listp;
	int block = 0;

	if (msblk->swap) {
		char sblock_list[blocks << 2];

		if (!squashfs3_get_cached_block(s, sblock_list, *start_block,
				*offset, blocks << 2, start_block, offset)) {
			ERROR("Fail reading block list [%llx:%x]\n", *start_block, *offset);
			goto failure;
		}
		SQUASHFS3_SWAP_INTS(((unsigned int *)block_list),
				((unsigned int *)sblock_list), blocks);
	} else {
		if (!squashfs3_get_cached_block(s, block_list, *start_block,
				*offset, blocks << 2, start_block, offset)) {
			ERROR("Fail reading block list [%llx:%x]\n", *start_block, *offset);
			goto failure;
		}
	}

	for (block_listp = (unsigned int *) block_list; blocks;
				block_listp++, blocks --)
		block += SQUASHFS3_COMPRESSED_SIZE_BLOCK(*block_listp);

	return block;

failure:
	return -1;
}


#define SIZE 256

static inline int calculate_skip(int blocks) {
	int skip = (blocks - 1) / ((SQUASHFS3_SLOTS * SQUASHFS3_META_ENTRIES + 1) * SQUASHFS3_META_INDEXES);
	return skip >= 7 ? 7 : skip + 1;
}


static int get_meta_index(struct inode *inode, int index,
		long long *index_block, int *index_offset,
		long long *data_block, char *block_list)
{
	struct squashfs3_sb_info *msblk = inode->i_sb->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	int skip = calculate_skip(i_size_read(inode) >> sblk->block_log);
	int offset = 0;
	struct squashfs3_meta_index *meta;
	struct squashfs3_meta_entry *meta_entry;
	long long cur_index_block = SQUASHFS3_I(inode)->u.s1.block_list_start;
	int cur_offset = SQUASHFS3_I(inode)->offset;
	long long cur_data_block = SQUASHFS3_I(inode)->start_block;
	int i;

	index /= SQUASHFS3_META_INDEXES * skip;

	while (offset < index) {
		meta = locate_meta_index(inode, index, offset + 1);

		if (meta == NULL) {
			meta = empty_meta_index(inode, offset + 1, skip);
			if (meta == NULL)
				goto all_done;
		} else {
			if(meta->entries == 0)
				goto failed;
			/* XXX */
			offset = index < meta->offset + meta->entries ? index :
				meta->offset + meta->entries - 1;
			/* XXX */
			meta_entry = &meta->meta_entry[offset - meta->offset];
			cur_index_block = meta_entry->index_block + sblk->inode_table_start;
			cur_offset = meta_entry->offset;
			cur_data_block = meta_entry->data_block;
			TRACE("get_meta_index: offset %d, meta->offset %d, "
				"meta->entries %d\n", offset, meta->offset, meta->entries);
			TRACE("get_meta_index: index_block 0x%llx, offset 0x%x"
				" data_block 0x%llx\n", cur_index_block,
				cur_offset, cur_data_block);
		}

		for (i = meta->offset + meta->entries; i <= index &&
				i < meta->offset + SQUASHFS3_META_ENTRIES; i++) {
			int blocks = skip * SQUASHFS3_META_INDEXES;

			while (blocks) {
				int block = blocks > (SIZE >> 2) ? (SIZE >> 2) : blocks;
				int res = read_block_index(inode->i_sb, block, block_list,
					&cur_index_block, &cur_offset);

				if (res == -1)
					goto failed;

				cur_data_block += res;
				blocks -= block;
			}

			meta_entry = &meta->meta_entry[i - meta->offset];
			meta_entry->index_block = cur_index_block - sblk->inode_table_start;
			meta_entry->offset = cur_offset;
			meta_entry->data_block = cur_data_block;
			meta->entries ++;
			offset ++;
		}

		TRACE("get_meta_index: meta->offset %d, meta->entries %d\n",
				meta->offset, meta->entries);

		release_meta_index(inode, meta);
	}

all_done:
	*index_block = cur_index_block;
	*index_offset = cur_offset;
	*data_block = cur_data_block;

	return offset * SQUASHFS3_META_INDEXES * skip;

failed:
	release_meta_index(inode, meta);
	return -1;
}


static long long read_blocklist(struct inode *inode, int index,
				int readahead_blks, char *block_list,
				unsigned short **block_p, unsigned int *bsize)
{
	long long block_ptr;
	int offset;
	long long block;
	int res = get_meta_index(inode, index, &block_ptr, &offset, &block,
		block_list);

	TRACE("read_blocklist: res %d, index %d, block_ptr 0x%llx, offset"
		       " 0x%x, block 0x%llx\n", res, index, block_ptr, offset, block);

	if(res == -1)
		goto failure;

	index -= res;

	while (index) {
		int blocks = index > (SIZE >> 2) ? (SIZE >> 2) : index;
		int res = read_block_index(inode->i_sb, blocks, block_list,
			&block_ptr, &offset);
		if (res == -1)
			goto failure;
		block += res;
		index -= blocks;
	}

	if (read_block_index(inode->i_sb, 1, block_list, &block_ptr, &offset) == -1)
		goto failure;
	*bsize = *((unsigned int *) block_list);

	return block;

failure:
	return 0;
}


static int squashfs3_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct squashfs3_sb_info *msblk = inode->i_sb->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	unsigned char *block_list = NULL;
	long long block;
	unsigned int bsize, i;
	int bytes;
	int index = page->index >> (sblk->block_log - PAGE_SHIFT);
	void *pageaddr;
	struct squashfs3_cache_entry *fragment = NULL;
	char *data_ptr = msblk->read_page;

	int mask = (1 << (sblk->block_log - PAGE_SHIFT)) - 1;
	int start_index = page->index & ~mask;
	int end_index = start_index | mask;
	int file_end = i_size_read(inode) >> sblk->block_log;
	int sparse = 0;

	TRACE("Entered squashfs3_readpage, page index %lx, start block %llx\n",
					page->index, SQUASHFS3_I(inode)->start_block);

	if (page->index >= ((i_size_read(inode) + PAGE_SIZE - 1) >>
					PAGE_SHIFT))
		goto out;

	if (SQUASHFS3_I(inode)->u.s1.fragment_start_block == SQUASHFS3_INVALID_BLK
					|| index < file_end) {
		block_list = kmalloc(SIZE, GFP_KERNEL);
		if (block_list == NULL) {
			ERROR("Failed to allocate block_list\n");
			goto error_out;
		}

		block = (msblk->read_blocklist)(inode, index, 1, block_list, NULL, &bsize);
		if (block == 0)
			goto error_out;

		if (bsize == 0) { /* hole */
			bytes = index == file_end ?
				(i_size_read(inode) & (sblk->block_size - 1)) : sblk->block_size;
			sparse = 1;
		} else {
			mutex_lock(&msblk->read_page_mutex);

			bytes = squashfs3_read_data(inode->i_sb, msblk->read_page, block,
				bsize, NULL, sblk->block_size);

			if (bytes == 0) {
				ERROR("Unable to read page, block %llx, size %x\n", block, bsize);
				mutex_unlock(&msblk->read_page_mutex);
				goto error_out;
			}
		}
	} else {
		fragment = get_cached_fragment(inode->i_sb,
					SQUASHFS3_I(inode)-> u.s1.fragment_start_block,
					SQUASHFS3_I(inode)->u.s1.fragment_size);

		if (fragment->error) {
			ERROR("Unable to read page, block %llx, size %x\n",
					SQUASHFS3_I(inode)->u.s1.fragment_start_block,
					(int) SQUASHFS3_I(inode)->u.s1.fragment_size);
			release_cached_fragment(msblk, fragment);
			goto error_out;
		}
		bytes = i_size_read(inode) & (sblk->block_size - 1);
		data_ptr = fragment->data + SQUASHFS3_I(inode)->u.s1.fragment_offset;
	}

	for (i = start_index; i <= end_index && bytes > 0; i++,
						bytes -= PAGE_SIZE, data_ptr += PAGE_SIZE) {
		struct page *push_page;
		int avail = sparse ? 0 : min_t(unsigned int, bytes, PAGE_SIZE);

		TRACE("bytes %d, i %d, available_bytes %d\n", bytes, i, avail);

		push_page = (i == page->index) ? page :
			grab_cache_page_nowait(page->mapping, i);

		if (!push_page)
			continue;

		if (PageUptodate(push_page))
			goto skip_page;

		pageaddr = kmap_atomic(push_page);
		memcpy(pageaddr, data_ptr, avail);
		memset(pageaddr + avail, 0, PAGE_SIZE - avail);
		kunmap_atomic(pageaddr);
		flush_dcache_page(push_page);
		SetPageUptodate(push_page);
skip_page:
		unlock_page(push_page);
		if(i != page->index)
			put_page(push_page);
	}

	if (SQUASHFS3_I(inode)->u.s1.fragment_start_block == SQUASHFS3_INVALID_BLK
					|| index < file_end) {
		if (!sparse)
			mutex_unlock(&msblk->read_page_mutex);
		kfree(block_list);
	} else
		release_cached_fragment(msblk, fragment);

	return 0;

error_out:
	SetPageError(page);
out:
	pageaddr = kmap_atomic(page);
	memset(pageaddr, 0, PAGE_SIZE);
	kunmap_atomic(pageaddr);
	flush_dcache_page(page);
	if (!PageError(page))
		SetPageUptodate(page);
	unlock_page(page);

	kfree(block_list);
	return 0;
}


static int get_dir_index_using_offset(struct super_block *s,
				long long *next_block, unsigned int *next_offset,
				long long index_start, unsigned int index_offset, int i_count,
				long long f_pos)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	int i, length = 0;
	struct squashfs3_dir_index index;

	TRACE("Entered get_dir_index_using_offset, i_count %d, f_pos %d\n",
					i_count, (unsigned int) f_pos);

	f_pos -= 3;
	if (f_pos == 0)
		goto finish;

	for (i = 0; i < i_count; i++) {
		if (msblk->swap) {
			struct squashfs3_dir_index sindex;
			squashfs3_get_cached_block(s, &sindex, index_start, index_offset,
					sizeof(sindex), &index_start, &index_offset);
			SQUASHFS3_SWAP_DIR_INDEX(&index, &sindex);
		} else
			squashfs3_get_cached_block(s, &index, index_start, index_offset,
					sizeof(index), &index_start, &index_offset);

		if (index.index > f_pos)
			break;

		squashfs3_get_cached_block(s, NULL, index_start, index_offset,
					index.size + 1, &index_start, &index_offset);

		length = index.index;
		*next_block = index.start_block + sblk->directory_table_start;
	}

	*next_offset = (length + *next_offset) % SQUASHFS3_METADATA_SIZE;

finish:
	return length + 3;
}


static int get_dir_index_using_name(struct super_block *s,
				long long *next_block, unsigned int *next_offset,
				long long index_start, unsigned int index_offset, int i_count,
				const char *name, int size)
{
	struct squashfs3_sb_info *msblk = s->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	int i, length = 0;
	struct squashfs3_dir_index *index;
	char *str;

	TRACE("Entered get_dir_index_using_name, i_count %d\n", i_count);

	str = kmalloc(sizeof(struct squashfs3_dir_index) +
		(SQUASHFS3_NAME_LEN + 1) * 2, GFP_KERNEL);
	if (str == NULL) {
		ERROR("Failed to allocate squashfs3_dir_index\n");
		goto failure;
	}

	index = (struct squashfs3_dir_index *) (str + SQUASHFS3_NAME_LEN + 1);
	strncpy(str, name, size);
	str[size] = '\0';

	for (i = 0; i < i_count; i++) {
		if (msblk->swap) {
			struct squashfs3_dir_index sindex;
			squashfs3_get_cached_block(s, &sindex, index_start, index_offset,
				sizeof(sindex), &index_start, &index_offset);
			SQUASHFS3_SWAP_DIR_INDEX(index, &sindex);
		} else
			squashfs3_get_cached_block(s, index, index_start, index_offset,
				sizeof(struct squashfs3_dir_index), &index_start, &index_offset);

		squashfs3_get_cached_block(s, index->name, index_start, index_offset,
					index->size + 1, &index_start, &index_offset);

		index->name[index->size + 1] = '\0';

		if (strcmp(index->name, str) > 0)
			break;

		length = index->index;
		*next_block = index->start_block + sblk->directory_table_start;
	}

	*next_offset = (length + *next_offset) % SQUASHFS3_METADATA_SIZE;
	kfree(str);

failure:
	return length + 3;
}


static int squashfs3_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *i = file_inode(file);
	struct squashfs3_sb_info *msblk = i->i_sb->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	long long next_block = SQUASHFS3_I(i)->start_block +
		sblk->directory_table_start;
	int next_offset = SQUASHFS3_I(i)->offset, length = 0, dir_count;
	struct squashfs3_dir_header dirh;
	struct squashfs3_dir_entry *dire;

	TRACE("Entered squashfs3_readdir [%llx:%x]\n", next_block, next_offset);

	dire = kmalloc(sizeof(struct squashfs3_dir_entry) +
		SQUASHFS3_NAME_LEN + 1, GFP_KERNEL);
	if (dire == NULL) {
		ERROR("Failed to allocate squashfs3_dir_entry\n");
		goto finish;
	}

	while(ctx->pos < 3) {
		char *name;
		int size, i_ino;

		if(ctx->pos == 0) {
			name = ".";
			size = 1;
			i_ino = i->i_ino;
		} else {
			name = "..";
			size = 2;
			i_ino = SQUASHFS3_I(i)->u.s2.parent_inode;
		}
		TRACE("Calling filldir(%x, %s, %d, %d, %d, %d)\n",
				(unsigned int) ctx, name, size, (int)
				ctx->pos, i_ino, squashfs3_filetype_table[1]);

		if (!dir_emit(ctx, name, size, i_ino,
				squashfs3_filetype_table[1])) {
				TRACE("Filldir failed\n");
			goto finish;
		}
		ctx->pos += size;
	}

	length = get_dir_index_using_offset(i->i_sb, &next_block, &next_offset,
				SQUASHFS3_I(i)->u.s2.directory_index_start,
				SQUASHFS3_I(i)->u.s2.directory_index_offset,
				SQUASHFS3_I(i)->u.s2.directory_index_count, ctx->pos);

	while (length < i_size_read(i)) {
		/* read directory header */
		if (msblk->swap) {
			struct squashfs3_dir_header sdirh;

			if (!squashfs3_get_cached_block(i->i_sb, &sdirh, next_block,
					 next_offset, sizeof(sdirh), &next_block, &next_offset))
				goto failed_read;

			length += sizeof(sdirh);
			SQUASHFS3_SWAP_DIR_HEADER(&dirh, &sdirh);
		} else {
			if (!squashfs3_get_cached_block(i->i_sb, &dirh, next_block,
					next_offset, sizeof(dirh), &next_block, &next_offset))
				goto failed_read;

			length += sizeof(dirh);
		}

		dir_count = dirh.count + 1;
		while (dir_count--) {
			if (msblk->swap) {
				struct squashfs3_dir_entry sdire;
				if (!squashfs3_get_cached_block(i->i_sb, &sdire, next_block,
						next_offset, sizeof(sdire), &next_block, &next_offset))
					goto failed_read;

				length += sizeof(sdire);
				SQUASHFS3_SWAP_DIR_ENTRY(dire, &sdire);
			} else {
				if (!squashfs3_get_cached_block(i->i_sb, dire, next_block,
						next_offset, sizeof(*dire), &next_block, &next_offset))
					goto failed_read;

				length += sizeof(*dire);
			}

			if (!squashfs3_get_cached_block(i->i_sb, dire->name, next_block,
						next_offset, dire->size + 1, &next_block, &next_offset))
				goto failed_read;

			length += dire->size + 1;

			if (ctx->pos >= length)
				continue;

			dire->name[dire->size + 1] = '\0';

			TRACE("Calling filldir(%x, %s, %d, %d, %x:%x, %d, %d)\n",
					(unsigned int) ctx, dire->name, dire->size + 1,
					(int) ctx->pos, dirh.start_block, dire->offset,
					dirh.inode_number + dire->inode_number,
					squashfs3_filetype_table[dire->type]);

			if (!dir_emit(ctx, dire->name, dire->size + 1,
					dirh.inode_number + dire->inode_number,
					squashfs3_filetype_table[dire->type])) {
				TRACE("Filldir failed\n");
				goto finish;
			}
			ctx->pos = length;
		}
	}

finish:
	kfree(dire);
	return 0;

failed_read:
	ERROR("Unable to read directory block [%llx:%x]\n", next_block,
		next_offset);
	kfree(dire);
	return 0;
}


static struct dentry *squashfs3_lookup(struct inode *i, struct dentry *dentry,
				unsigned int flags)
{
	const unsigned char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	struct inode *inode = NULL;
	struct squashfs3_sb_info *msblk = i->i_sb->s_fs_info;
	struct squashfs3_super_block *sblk = &msblk->sblk;
	long long next_block = SQUASHFS3_I(i)->start_block +
				sblk->directory_table_start;
	int next_offset = SQUASHFS3_I(i)->offset, length = 0, dir_count;
	struct squashfs3_dir_header dirh;
	struct squashfs3_dir_entry *dire;

	TRACE("Entered squashfs3_lookup [%llx:%x]\n", next_block, next_offset);

	dire = kmalloc(sizeof(struct squashfs3_dir_entry) +
		SQUASHFS3_NAME_LEN + 1, GFP_KERNEL);
	if (dire == NULL) {
		ERROR("Failed to allocate squashfs3_dir_entry\n");
		goto exit_lookup;
	}

	if (len > SQUASHFS3_NAME_LEN)
		goto exit_lookup;

	length = get_dir_index_using_name(i->i_sb, &next_block, &next_offset,
				SQUASHFS3_I(i)->u.s2.directory_index_start,
				SQUASHFS3_I(i)->u.s2.directory_index_offset,
				SQUASHFS3_I(i)->u.s2.directory_index_count, name, len);

	while (length < i_size_read(i)) {
		/* read directory header */
		if (msblk->swap) {
			struct squashfs3_dir_header sdirh;
			if (!squashfs3_get_cached_block(i->i_sb, &sdirh, next_block,
					 next_offset, sizeof(sdirh), &next_block, &next_offset))
				goto failed_read;

			length += sizeof(sdirh);
			SQUASHFS3_SWAP_DIR_HEADER(&dirh, &sdirh);
		} else {
			if (!squashfs3_get_cached_block(i->i_sb, &dirh, next_block,
					next_offset, sizeof(dirh), &next_block, &next_offset))
				goto failed_read;

			length += sizeof(dirh);
		}

		dir_count = dirh.count + 1;
		while (dir_count--) {
			if (msblk->swap) {
				struct squashfs3_dir_entry sdire;
				if (!squashfs3_get_cached_block(i->i_sb, &sdire, next_block,
						next_offset, sizeof(sdire), &next_block, &next_offset))
					goto failed_read;

				length += sizeof(sdire);
				SQUASHFS3_SWAP_DIR_ENTRY(dire, &sdire);
			} else {
				if (!squashfs3_get_cached_block(i->i_sb, dire, next_block,
						next_offset, sizeof(*dire), &next_block, &next_offset))
					goto failed_read;

				length += sizeof(*dire);
			}

			if (!squashfs3_get_cached_block(i->i_sb, dire->name, next_block,
					next_offset, dire->size + 1, &next_block, &next_offset))
				goto failed_read;

			length += dire->size + 1;

			if (name[0] < dire->name[0])
				goto exit_lookup;

			if ((len == dire->size + 1) && !strncmp(name, dire->name, len)) {
				squashfs3_inode_t ino = SQUASHFS3_MKINODE(dirh.start_block,
								dire->offset);

				TRACE("calling squashfs3_iget for directory entry %s, inode"
					"  %x:%x, %d\n", name, dirh.start_block, dire->offset,
					dirh.inode_number + dire->inode_number);

				inode = squashfs3_iget(i->i_sb, ino, dirh.inode_number + dire->inode_number);

				goto exit_lookup;
			}
		}
	}

exit_lookup:
	kfree(dire);
	if (inode)
		return d_splice_alias(inode, dentry);
	d_add(dentry, inode);
	return ERR_PTR(0);

failed_read:
	ERROR("Unable to read directory block [%llx:%x]\n", next_block,
		next_offset);
	goto exit_lookup;
}


static int squashfs3_remount(struct super_block *s, int *flags, char *data)
{
	*flags |= MS_RDONLY;
	return 0;
}


static void squashfs3_put_super(struct super_block *s)
{
	if (s->s_fs_info) {
		struct squashfs3_sb_info *sbi = s->s_fs_info;
		squashfs3_cache_delete(sbi->block_cache);
		squashfs3_cache_delete(sbi->fragment_cache);
		vfree(sbi->read_page);
		kfree(sbi->uid);
		kfree(sbi->fragment_index);
		kfree(sbi->fragment_index_2);
		kfree(sbi->meta_index);
		vfree(sbi->stream.workspace);
		kfree(s->s_fs_info);
		s->s_fs_info = NULL;
	}
}


static struct dentry *squashfs3_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, squashfs3_fill_super);
}


static int __init init_squashfs3_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out;

	printk(KERN_INFO "squashfs3: version 3.4 (2008/08/26) "
		"Phillip Lougher\n");

	err = register_filesystem(&squashfs3_fs_type);
	if (err)
		destroy_inodecache();

out:
	return err;
}


static void __exit exit_squashfs3_fs(void)
{
	unregister_filesystem(&squashfs3_fs_type);
	destroy_inodecache();
}


static struct kmem_cache * squashfs3_inode_cachep;


static struct inode *squashfs3_alloc_inode(struct super_block *sb)
{
	struct squashfs3_inode_info *ei;
	ei = kmem_cache_alloc(squashfs3_inode_cachep, GFP_KERNEL);
	return ei ? &ei->vfs_inode : NULL;
}


static void squashfs3_destroy_inode(struct inode *inode)
{
	kmem_cache_free(squashfs3_inode_cachep, SQUASHFS3_I(inode));
}


static void init_once(void *foo)
{
	struct squashfs3_inode_info *ei = foo;

	inode_init_once(&ei->vfs_inode);
}


static int __init init_inodecache(void)
{
	squashfs3_inode_cachep = kmem_cache_create("squashfs3_inode_cache",
	    sizeof(struct squashfs3_inode_info), 0,
		SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT, init_once);
	if (squashfs3_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}


static void destroy_inodecache(void)
{
	kmem_cache_destroy(squashfs3_inode_cachep);
}


module_init(init_squashfs3_fs);
module_exit(exit_squashfs3_fs);
MODULE_DESCRIPTION("squashfs3 3.4, a compressed read-only filesystem");
MODULE_AUTHOR("Phillip Lougher <phillip@lougher.demon.co.uk>");
MODULE_LICENSE("GPL");
