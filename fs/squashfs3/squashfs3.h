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
 * squashfs3.h
 */

#ifdef CONFIG_SQUASHFS3_1_0_COMPATIBILITY
#undef CONFIG_SQUASHFS3_1_0_COMPATIBILITY
#endif

#ifdef SQUASHFS3_TRACE
#define TRACE(s, args...)	printk(KERN_NOTICE "SQUASHFS3: "s, ## args)
#else
#define TRACE(s, args...)	{}
#endif

#define ERROR(s, args...)	printk(KERN_ERR "SQUASHFS3 error: "s, ## args)

#define SERROR(s, args...)	do { \
				if (!silent) \
				printk(KERN_ERR "SQUASHFS3 error: "s, ## args);\
				} while(0)

#define WARNING(s, args...)	printk(KERN_WARNING "SQUASHFS3: "s, ## args)

static inline struct squashfs3_inode_info *SQUASHFS3_I(struct inode *inode)
{
	return list_entry(inode, struct squashfs3_inode_info, vfs_inode);
}

#if defined(CONFIG_SQUASHFS3_1_0_COMPATIBILITY ) || defined(CONFIG_SQUASHFS3_2_0_COMPATIBILITY)
#define SQSH_EXTERN
extern unsigned int squashfs3_read_data(struct super_block *s, char *buffer,
				long long index, unsigned int length,
				long long *next_index, int srclength);
extern int squashfs3_get_cached_block(struct super_block *s, void *buffer,
				long long block, unsigned int offset,
				int length, long long *next_block,
				unsigned int *next_offset);
extern struct inode *squashfs3_iget(struct super_block *s, squashfs3_inode_t inode, unsigned int inode_number);
extern const struct address_space_operations squashfs3_symlink_aops;
extern const struct address_space_operations squashfs3_aops;
extern struct inode_operations squashfs3_dir_inode_ops;
#else
#define SQSH_EXTERN static
#endif

#ifdef CONFIG_SQUASHFS3_1_0_COMPATIBILITY
extern int squashfs3_1_0_supported(struct squashfs3_sb_info *msblk);
#else
static inline int squashfs3_1_0_supported(struct squashfs3_sb_info *msblk)
{
	return 0;
}
#endif

#ifdef CONFIG_SQUASHFS3_2_0_COMPATIBILITY
extern int squashfs3_2_0_supported(struct squashfs3_sb_info *msblk);
#else
static inline int squashfs3_2_0_supported(struct squashfs3_sb_info *msblk)
{
	return 0;
}
#endif
