/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM filemap

#if !defined(_TRACE_FILEMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FILEMAP_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mm.h>
#include <linux/memcontrol.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/errseq.h>

DECLARE_EVENT_CLASS(mm_filemap_op_page_cache,

	TP_PROTO(struct folio *folio),

	TP_ARGS(folio),

	TP_STRUCT__entry(
		__field(unsigned long, pfn)
		__field(unsigned long, i_ino)
		__field(unsigned long, index)
		__field(dev_t, s_dev)
		__field(unsigned char, order)
	),

	TP_fast_assign(
		__entry->pfn = folio_pfn(folio);
		__entry->i_ino = folio->mapping->host->i_ino;
		__entry->index = folio->index;
		if (folio->mapping->host->i_sb)
			__entry->s_dev = inode_get_dev(folio->mapping->host);
		else
			__entry->s_dev = folio->mapping->host->i_rdev;
		__entry->order = folio_order(folio);
	),

	TP_printk("dev %d:%d ino %lx pfn=0x%lx ofs=%lu order=%u",
		MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
		__entry->i_ino,
		__entry->pfn,
		__entry->index << PAGE_SHIFT,
		__entry->order)
);

DEFINE_EVENT(mm_filemap_op_page_cache, mm_filemap_delete_from_page_cache,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio)
	);

DEFINE_EVENT(mm_filemap_op_page_cache, mm_filemap_add_to_page_cache,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio)
	);

DECLARE_EVENT_CLASS(mm_filemap_op_page_cache_range,

	TP_PROTO(
		struct address_space *mapping,
		pgoff_t index,
		pgoff_t last_index
	),

	TP_ARGS(mapping, index, last_index),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(unsigned long, index)
		__field(unsigned long, last_index)
	),

	TP_fast_assign(
		__entry->i_ino = mapping->host->i_ino;
		if (mapping->host->i_sb)
			__entry->s_dev =
				mapping->host->i_sb->s_dev;
		else
			__entry->s_dev = mapping->host->i_rdev;
		__entry->index = index;
		__entry->last_index = last_index;
	),

	TP_printk(
		"dev=%d:%d ino=%lx ofs=%lld-%lld",
		MAJOR(__entry->s_dev),
		MINOR(__entry->s_dev), __entry->i_ino,
		((loff_t)__entry->index) << PAGE_SHIFT,
		((((loff_t)__entry->last_index + 1) << PAGE_SHIFT) - 1)
	)
);

DEFINE_EVENT(mm_filemap_op_page_cache_range, mm_filemap_get_pages,
	TP_PROTO(
		struct address_space *mapping,
		pgoff_t index,
		pgoff_t last_index
	),
	TP_ARGS(mapping, index, last_index)
);

DEFINE_EVENT(mm_filemap_op_page_cache_range, mm_filemap_map_pages,
	TP_PROTO(
		struct address_space *mapping,
		pgoff_t index,
		pgoff_t last_index
	),
	TP_ARGS(mapping, index, last_index)
);

TRACE_EVENT(mm_filemap_fault,
	TP_PROTO(struct address_space *mapping, pgoff_t index),

	TP_ARGS(mapping, index),

	TP_STRUCT__entry(
		__field(unsigned long, i_ino)
		__field(dev_t, s_dev)
		__field(unsigned long, index)
	),

	TP_fast_assign(
		__entry->i_ino = mapping->host->i_ino;
		if (mapping->host->i_sb)
			__entry->s_dev =
				mapping->host->i_sb->s_dev;
		else
			__entry->s_dev = mapping->host->i_rdev;
		__entry->index = index;
	),

	TP_printk(
		"dev=%d:%d ino=%lx ofs=%lld",
		MAJOR(__entry->s_dev),
		MINOR(__entry->s_dev), __entry->i_ino,
		((loff_t)__entry->index) << PAGE_SHIFT
	)
);

TRACE_EVENT(filemap_set_wb_err,
		TP_PROTO(struct address_space *mapping, errseq_t eseq),

		TP_ARGS(mapping, eseq),

		TP_STRUCT__entry(
			__field(unsigned long, i_ino)
			__field(dev_t, s_dev)
			__field(errseq_t, errseq)
		),

		TP_fast_assign(
			__entry->i_ino = mapping->host->i_ino;
			__entry->errseq = eseq;
			if (mapping->host->i_sb)
				__entry->s_dev = inode_get_dev(mapping->host);
			else
				__entry->s_dev = mapping->host->i_rdev;
		),

		TP_printk("dev=%d:%d ino=0x%lx errseq=0x%x",
			MAJOR(__entry->s_dev), MINOR(__entry->s_dev),
			__entry->i_ino, __entry->errseq)
);

TRACE_EVENT(file_check_and_advance_wb_err,
		TP_PROTO(struct file *file, errseq_t old),

		TP_ARGS(file, old),

		TP_STRUCT__entry(
			__field(struct file *, file)
			__field(unsigned long, i_ino)
			__field(dev_t, s_dev)
			__field(errseq_t, old)
			__field(errseq_t, new)
		),

		TP_fast_assign(
			__entry->file = file;
			__entry->i_ino = file->f_mapping->host->i_ino;
			if (file->f_mapping->host->i_sb)
				__entry->s_dev =
					inode_get_dev(file->f_mapping->host);
			else
				__entry->s_dev =
					file->f_mapping->host->i_rdev;
			__entry->old = old;
			__entry->new = file->f_wb_err;
		),

		TP_printk("file=%p dev=%d:%d ino=0x%lx old=0x%x new=0x%x",
			__entry->file, MAJOR(__entry->s_dev),
			MINOR(__entry->s_dev), __entry->i_ino, __entry->old,
			__entry->new)
);
#endif /* _TRACE_FILEMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
