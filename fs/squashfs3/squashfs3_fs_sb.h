#ifndef SQUASHFS3_FS_SB
#define SQUASHFS3_FS_SB
/*
 * Squashfs3
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
 * squashfs3_fs_sb.h
 */

#include "squashfs3_fs.h"

struct squashfs3_cache_entry {
	long long	block;
	int		length;
	int		locked;
	long long	next_index;
	char		pending;
	char		error;
	int		waiting;
	wait_queue_head_t	wait_queue;
	char		*data;
};

struct squashfs3_cache {
	char *name;
	int entries;
	int block_size;
	int next_blk;
	int waiting;
	int unused_blks;
	int use_vmalloc;
	spinlock_t lock;
	wait_queue_head_t wait_queue;
	struct squashfs3_cache_entry entry[0];
};

struct squashfs3_sb_info {
	struct squashfs3_super_block	sblk;
	int			devblksize;
	int			devblksize_log2;
	int			swap;
	struct squashfs3_cache	*block_cache;
	struct squashfs3_cache	*fragment_cache;
	int			next_meta_index;
	unsigned int		*uid;
	unsigned int		*guid;
	long long		*fragment_index;
	unsigned int		*fragment_index_2;
	char			*read_page;
	struct mutex		read_data_mutex;
	struct mutex		read_page_mutex;
	struct mutex		meta_index_mutex;
	struct squashfs3_meta_index	*meta_index;
	z_stream		stream;
	long long		*inode_lookup_table;
	int			(*read_inode)(struct inode *i,  squashfs3_inode_t \
				inode);
	long long		(*read_blocklist)(struct inode *inode, int \
				index, int readahead_blks, char *block_list, \
				unsigned short **block_p, unsigned int *bsize);
	int			(*read_fragment_index_table)(struct super_block *s);
};
#endif
