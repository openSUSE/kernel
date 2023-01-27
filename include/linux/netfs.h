/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Network filesystem support services.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See:
 *
 *	Documentation/filesystems/netfs_library.rst
 *
 * for a description of the network filesystem interface declared here.
 */

#ifndef _LINUX_NETFS_H
#define _LINUX_NETFS_H

#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

enum netfs_sreq_ref_trace;

/*
 * Overload PG_private_2 to give us PG_fscache - this is used to indicate that
 * a page is currently backed by a local disk cache
 */
#define PageFsCache(page)		PagePrivate2((page))
#define SetPageFsCache(page)		SetPagePrivate2((page))
#define ClearPageFsCache(page)		ClearPagePrivate2((page))
#define TestSetPageFsCache(page)	TestSetPagePrivate2((page))
#define TestClearPageFsCache(page)	TestClearPagePrivate2((page))

/**
 * set_page_fscache - Set PG_fscache on a page and take a ref
 * @page: The page.
 *
 * Set the PG_fscache (PG_private_2) flag on a page and take the reference
 * needed for the VM to handle its lifetime correctly.  This sets the flag and
 * takes the reference unconditionally, so care must be taken not to set the
 * flag again if it's already set.
 */
static inline void set_page_fscache(struct page *page)
{
	set_page_private_2(page);
}

/**
 * end_page_fscache - Clear PG_fscache and release any waiters
 * @page: The page
 *
 * Clear the PG_fscache (PG_private_2) bit on a page and wake up any sleepers
 * waiting for this.  The page ref held for PG_private_2 being set is released.
 *
 * This is, for example, used when a netfs page is being written to a local
 * disk cache, thereby allowing writes to the cache for the same page to be
 * serialised.
 */
static inline void end_page_fscache(struct page *page)
{
	end_page_private_2(page);
}

/**
 * wait_on_page_fscache - Wait for PG_fscache to be cleared on a page
 * @page: The page to wait on
 *
 * Wait for PG_fscache (aka PG_private_2) to be cleared on a page.
 */
static inline void wait_on_page_fscache(struct page *page)
{
	wait_on_page_private_2(page);
}

/**
 * wait_on_page_fscache_killable - Wait for PG_fscache to be cleared on a page
 * @page: The page to wait on
 *
 * Wait for PG_fscache (aka PG_private_2) to be cleared on a page or until a
 * fatal signal is received by the calling task.
 *
 * Return:
 * - 0 if successful.
 * - -EINTR if a fatal signal was encountered.
 */
static inline int wait_on_page_fscache_killable(struct page *page)
{
	return wait_on_page_private_2_killable(page);
}

enum netfs_io_source {
	NETFS_FILL_WITH_ZEROES,
	NETFS_DOWNLOAD_FROM_SERVER,
	NETFS_READ_FROM_CACHE,
	NETFS_INVALID_READ,
} __mode(byte);

typedef void (*netfs_io_terminated_t)(void *priv, ssize_t transferred_or_error,
				      bool was_async);

/*
 * Resources required to do operations on a cache.
 */
struct netfs_cache_resources {
	const struct netfs_cache_ops	*ops;
	void				*cache_priv;
	void				*cache_priv2;
	unsigned int			debug_id;	/* Cookie debug ID */
	unsigned int			inval_counter;	/* object->inval_counter at begin_op */
};

/*
 * Descriptor for a single component subrequest.
 */
struct netfs_io_subrequest {
	struct netfs_io_request *rreq;		/* Supervising I/O request */
	struct list_head	rreq_link;	/* Link in rreq->subrequests */
	loff_t			start;		/* Where to start the I/O */
	size_t			len;		/* Size of the I/O */
	size_t			transferred;	/* Amount of data transferred */
	refcount_t		ref;
	short			error;		/* 0 or error that occurred */
	unsigned short		debug_index;	/* Index in list (for debugging output) */
	enum netfs_io_source	source;		/* Where to read from/write to */
	unsigned long		flags;
#define NETFS_SREQ_COPY_TO_CACHE	0	/* Set if should copy the data to the cache */
#define NETFS_SREQ_CLEAR_TAIL		1	/* Set if the rest of the read should be cleared */
#define NETFS_SREQ_SHORT_IO		2	/* Set if the I/O was short */
#define NETFS_SREQ_SEEK_DATA_READ	3	/* Set if ->read() should SEEK_DATA first */
#define NETFS_SREQ_NO_PROGRESS		4	/* Set if we didn't manage to read any data */
};

/*
 * Descriptor for an I/O helper request.  This is used to make multiple I/O
 * operations to a variety of data stores and then stitch the result together.
 */
struct netfs_io_request {
	struct work_struct	work;
	struct inode		*inode;		/* The file being accessed */
	struct address_space	*mapping;	/* The mapping being accessed */
	struct netfs_cache_resources cache_resources;
	struct list_head	subrequests;	/* Contributory I/O operations */
	void			*netfs_priv;	/* Private data for the netfs */
	unsigned int		debug_id;
	atomic_t		nr_outstanding;	/* Number of ops in progress */
	atomic_t		nr_copy_ops;	/* Number of copy-to-cache ops in progress */
	size_t			submitted;	/* Amount submitted for I/O so far */
	size_t			len;		/* Length of the request */
	short			error;		/* 0 or error that occurred */
	loff_t			i_size;		/* Size of the file */
	loff_t			start;		/* Start position */
	pgoff_t			no_unlock_page;	/* Don't unlock this page after read */
	refcount_t		ref;
	unsigned long		flags;
#define NETFS_RREQ_INCOMPLETE_IO	0	/* Some ioreqs terminated short or with error */
#define NETFS_RREQ_COPY_TO_CACHE	1	/* Need to write to the cache */
#define NETFS_RREQ_NO_UNLOCK_PAGE	2	/* Don't unlock no_unlock_page on completion */
#define NETFS_RREQ_DONT_UNLOCK_PAGES	3	/* Don't unlock the pages on completion */
#define NETFS_RREQ_FAILED		4	/* The request failed */
#define NETFS_RREQ_IN_PROGRESS		5	/* Unlocked when the request completes */
	const struct netfs_request_ops *netfs_ops;
};

/*
 * Operations the network filesystem can/must provide to the helpers.
 */
struct netfs_request_ops {
	bool (*is_cache_enabled)(struct inode *inode);
	void (*init_request)(struct netfs_io_request *rreq, struct file *file);
	int (*begin_cache_operation)(struct netfs_io_request *rreq);
	void (*expand_readahead)(struct netfs_io_request *rreq);
	bool (*clamp_length)(struct netfs_io_subrequest *subreq);
	void (*issue_read)(struct netfs_io_subrequest *subreq);
	bool (*is_still_valid)(struct netfs_io_request *rreq);
	int (*check_write_begin)(struct file *file, loff_t pos, unsigned len,
				 struct page *page, void **_fsdata);
	void (*done)(struct netfs_io_request *rreq);
	void (*cleanup)(struct address_space *mapping, void *netfs_priv);
};

/*
 * How to handle reading from a hole.
 */
enum netfs_read_from_hole {
	NETFS_READ_HOLE_IGNORE,
	NETFS_READ_HOLE_CLEAR,
	NETFS_READ_HOLE_FAIL,
};

/*
 * Table of operations for access to a cache.  This is obtained by
 * rreq->ops->begin_cache_operation().
 */
struct netfs_cache_ops {
	/* End an operation */
	void (*end_operation)(struct netfs_cache_resources *cres);

	/* Read data from the cache */
	int (*read)(struct netfs_cache_resources *cres,
		    loff_t start_pos,
		    struct iov_iter *iter,
		    enum netfs_read_from_hole read_hole,
		    netfs_io_terminated_t term_func,
		    void *term_func_priv);

	/* Write data to the cache */
	int (*write)(struct netfs_cache_resources *cres,
		     loff_t start_pos,
		     struct iov_iter *iter,
		     netfs_io_terminated_t term_func,
		     void *term_func_priv);

	/* Expand readahead request */
	void (*expand_readahead)(struct netfs_cache_resources *cres,
				 loff_t *_start, size_t *_len, loff_t i_size);

	/* Prepare a read operation, shortening it to a cached/uncached
	 * boundary as appropriate.
	 */
	enum netfs_io_source (*prepare_read)(struct netfs_io_subrequest *subreq,
					       loff_t i_size);

	/* Prepare a write operation, working out what part of the write we can
	 * actually do.
	 */
	int (*prepare_write)(struct netfs_cache_resources *cres,
			     loff_t *_start, size_t *_len, loff_t i_size,
			     bool no_space_allocated_yet);

	/* Query the occupancy of the cache in a region, returning where the
	 * next chunk of data starts and how long it is.
	 */
	int (*query_occupancy)(struct netfs_cache_resources *cres,
			       loff_t start, size_t len, size_t granularity,
			       loff_t *_data_start, size_t *_data_len);
};

struct readahead_control;
extern void netfs_readahead(struct readahead_control *,
			    const struct netfs_request_ops *,
			    void *);
extern int netfs_readpage(struct file *,
			  struct page *,
			  const struct netfs_request_ops *,
			  void *);
extern int netfs_write_begin(struct file *, struct address_space *,
			     loff_t, unsigned int, unsigned int, struct page **,
			     void **,
			     const struct netfs_request_ops *,
			     void *);

extern void netfs_subreq_terminated(struct netfs_io_subrequest *, ssize_t, bool);
extern void netfs_get_subrequest(struct netfs_io_subrequest *subreq,
				 enum netfs_sreq_ref_trace what);
extern void netfs_put_subrequest(struct netfs_io_subrequest *subreq,
				 bool was_async, enum netfs_sreq_ref_trace what);
extern void netfs_stats_show(struct seq_file *);

#endif /* _LINUX_NETFS_H */
