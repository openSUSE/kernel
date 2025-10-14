// SPDX-License-Identifier: GPL-2.0-only
/* Miscellaneous routines.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/swap.h>
#include "internal.h"

/**
 * netfs_alloc_folioq_buffer - Allocate buffer space into a folio queue
 * @mapping: Address space to set on the folio (or NULL).
 * @_buffer: Pointer to the folio queue to add to (may point to a NULL; updated).
 * @_cur_size: Current size of the buffer (updated).
 * @size: Target size of the buffer.
 * @gfp: The allocation constraints.
 */
int netfs_alloc_folioq_buffer(struct address_space *mapping,
			      struct folio_queue **_buffer,
			      size_t *_cur_size, ssize_t size, gfp_t gfp)
{
	struct folio_queue *tail = *_buffer, *p;

	size = round_up(size, PAGE_SIZE);
	if (*_cur_size >= size)
		return 0;

	if (tail)
		while (tail->next)
			tail = tail->next;

	do {
		struct folio *folio;
		int order = 0, slot;

		if (!tail || folioq_full(tail)) {
			p = netfs_folioq_alloc(0, GFP_NOFS, netfs_trace_folioq_alloc_buffer);
			if (!p)
				return -ENOMEM;
			if (tail) {
				tail->next = p;
				p->prev = tail;
			} else {
				*_buffer = p;
			}
			tail = p;
		}

		if (size - *_cur_size > PAGE_SIZE)
			order = umin(ilog2(size - *_cur_size) - PAGE_SHIFT,
				     MAX_PAGECACHE_ORDER);

		folio = folio_alloc(gfp, order);
		if (!folio && order > 0)
			folio = folio_alloc(gfp, 0);
		if (!folio)
			return -ENOMEM;

		folio->mapping = mapping;
		folio->index = *_cur_size / PAGE_SIZE;
		trace_netfs_folio(folio, netfs_folio_trace_alloc_buffer);
		slot = folioq_append_mark(tail, folio);
		*_cur_size += folioq_folio_size(tail, slot);
	} while (*_cur_size < size);

	return 0;
}
EXPORT_SYMBOL(netfs_alloc_folioq_buffer);

/**
 * netfs_free_folioq_buffer - Free a folio queue.
 * @fq: The start of the folio queue to free
 *
 * Free up a chain of folio_queues and, if marked, the marked folios they point
 * to.
 */
void netfs_free_folioq_buffer(struct folio_queue *fq)
{
	struct folio_queue *next;
	struct folio_batch fbatch;

	folio_batch_init(&fbatch);

	for (; fq; fq = next) {
		for (int slot = 0; slot < folioq_count(fq); slot++) {
			struct folio *folio = folioq_folio(fq, slot);

			if (!folio ||
			    !folioq_is_marked(fq, slot))
				continue;

			trace_netfs_folio(folio, netfs_folio_trace_put);
			if (folio_batch_add(&fbatch, folio))
				folio_batch_release(&fbatch);
		}

		netfs_stat_d(&netfs_n_folioq);
		next = fq->next;
		kfree(fq);
	}

	folio_batch_release(&fbatch);
}
EXPORT_SYMBOL(netfs_free_folioq_buffer);

/*
 * Reset the subrequest iterator to refer just to the region remaining to be
 * read.  The iterator may or may not have been advanced by socket ops or
 * extraction ops to an extent that may or may not match the amount actually
 * read.
 */
void netfs_reset_iter(struct netfs_io_subrequest *subreq)
{
	struct iov_iter *io_iter = &subreq->io_iter;
	size_t remain = subreq->len - subreq->transferred;

	if (io_iter->count > remain)
		iov_iter_advance(io_iter, io_iter->count - remain);
	else if (io_iter->count < remain)
		iov_iter_revert(io_iter, remain - io_iter->count);
	iov_iter_truncate(&subreq->io_iter, remain);
}

/**
 * netfs_dirty_folio - Mark folio dirty and pin a cache object for writeback
 * @mapping: The mapping the folio belongs to.
 * @folio: The folio being dirtied.
 *
 * Set the dirty flag on a folio and pin an in-use cache object in memory so
 * that writeback can later write to it.  This is intended to be called from
 * the filesystem's ->dirty_folio() method.
 *
 * Return: true if the dirty flag was set on the folio, false otherwise.
 */
bool netfs_dirty_folio(struct address_space *mapping, struct folio *folio)
{
	struct inode *inode = mapping->host;
	struct netfs_inode *ictx = netfs_inode(inode);
	struct fscache_cookie *cookie = netfs_i_cookie(ictx);
	bool need_use = false;

	_enter("");

	if (!filemap_dirty_folio(mapping, folio))
		return false;
	if (!fscache_cookie_valid(cookie))
		return true;

	if (!(inode->i_state & I_PINNING_NETFS_WB)) {
		spin_lock(&inode->i_lock);
		if (!(inode->i_state & I_PINNING_NETFS_WB)) {
			inode->i_state |= I_PINNING_NETFS_WB;
			need_use = true;
		}
		spin_unlock(&inode->i_lock);

		if (need_use)
			fscache_use_cookie(cookie, true);
	}
	return true;
}
EXPORT_SYMBOL(netfs_dirty_folio);

/**
 * netfs_unpin_writeback - Unpin writeback resources
 * @inode: The inode on which the cookie resides
 * @wbc: The writeback control
 *
 * Unpin the writeback resources pinned by netfs_dirty_folio().  This is
 * intended to be called as/by the netfs's ->write_inode() method.
 */
int netfs_unpin_writeback(struct inode *inode, struct writeback_control *wbc)
{
	struct fscache_cookie *cookie = netfs_i_cookie(netfs_inode(inode));

	if (wbc->unpinned_netfs_wb)
		fscache_unuse_cookie(cookie, NULL, NULL);
	return 0;
}
EXPORT_SYMBOL(netfs_unpin_writeback);

/**
 * netfs_clear_inode_writeback - Clear writeback resources pinned by an inode
 * @inode: The inode to clean up
 * @aux: Auxiliary data to apply to the inode
 *
 * Clear any writeback resources held by an inode when the inode is evicted.
 * This must be called before clear_inode() is called.
 */
void netfs_clear_inode_writeback(struct inode *inode, const void *aux)
{
	struct fscache_cookie *cookie = netfs_i_cookie(netfs_inode(inode));

	if (inode->i_state & I_PINNING_NETFS_WB) {
		loff_t i_size = i_size_read(inode);
		fscache_unuse_cookie(cookie, aux, &i_size);
	}
}
EXPORT_SYMBOL(netfs_clear_inode_writeback);

/**
 * netfs_invalidate_folio - Invalidate or partially invalidate a folio
 * @folio: Folio proposed for release
 * @offset: Offset of the invalidated region
 * @length: Length of the invalidated region
 *
 * Invalidate part or all of a folio for a network filesystem.  The folio will
 * be removed afterwards if the invalidated region covers the entire folio.
 */
void netfs_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	struct netfs_folio *finfo;
	struct netfs_inode *ctx = netfs_inode(folio_inode(folio));
	size_t flen = folio_size(folio);

	_enter("{%lx},%zx,%zx", folio->index, offset, length);

	if (offset == 0 && length == flen) {
		unsigned long long i_size = i_size_read(&ctx->inode);
		unsigned long long fpos = folio_pos(folio), end;

		end = umin(fpos + flen, i_size);
		if (fpos < i_size && end > ctx->zero_point)
			ctx->zero_point = end;
	}

	folio_wait_private_2(folio); /* [DEPRECATED] */

	if (!folio_test_private(folio))
		return;

	finfo = netfs_folio_info(folio);

	if (offset == 0 && length >= flen)
		goto erase_completely;

	if (finfo) {
		/* We have a partially uptodate page from a streaming write. */
		unsigned int fstart = finfo->dirty_offset;
		unsigned int fend = fstart + finfo->dirty_len;
		unsigned int iend = offset + length;

		if (offset >= fend)
			return;
		if (iend <= fstart)
			return;

		/* The invalidation region overlaps the data.  If the region
		 * covers the start of the data, we either move along the start
		 * or just erase the data entirely.
		 */
		if (offset <= fstart) {
			if (iend >= fend)
				goto erase_completely;
			/* Move the start of the data. */
			finfo->dirty_len = fend - iend;
			finfo->dirty_offset = offset;
			return;
		}

		/* Reduce the length of the data if the invalidation region
		 * covers the tail part.
		 */
		if (iend >= fend) {
			finfo->dirty_len = offset - fstart;
			return;
		}

		/* A partial write was split.  The caller has already zeroed
		 * it, so just absorb the hole.
		 */
	}
	return;

erase_completely:
	netfs_put_group(netfs_folio_group(folio));
	folio_detach_private(folio);
	folio_clear_uptodate(folio);
	kfree(finfo);
	return;
}
EXPORT_SYMBOL(netfs_invalidate_folio);

/**
 * netfs_release_folio - Try to release a folio
 * @folio: Folio proposed for release
 * @gfp: Flags qualifying the release
 *
 * Request release of a folio and clean up its private state if it's not busy.
 * Returns true if the folio can now be released, false if not
 */
bool netfs_release_folio(struct folio *folio, gfp_t gfp)
{
	struct netfs_inode *ctx = netfs_inode(folio_inode(folio));
	unsigned long long end;

	if (folio_test_dirty(folio))
		return false;

	end = umin(folio_pos(folio) + folio_size(folio), i_size_read(&ctx->inode));
	if (end > ctx->zero_point)
		ctx->zero_point = end;

	if (folio_test_private(folio))
		return false;
	if (unlikely(folio_test_private_2(folio))) { /* [DEPRECATED] */
		if (current_is_kswapd() || !(gfp & __GFP_FS))
			return false;
		folio_wait_private_2(folio);
	}
	fscache_note_page_release(netfs_i_cookie(ctx));
	return true;
}
EXPORT_SYMBOL(netfs_release_folio);

/*
 * Wake the collection work item.
 */
void netfs_wake_collector(struct netfs_io_request *rreq)
{
	if (test_bit(NETFS_RREQ_OFFLOAD_COLLECTION, &rreq->flags) &&
	    !test_bit(NETFS_RREQ_RETRYING, &rreq->flags)) {
		queue_work(system_dfl_wq, &rreq->work);
	} else {
		trace_netfs_rreq(rreq, netfs_rreq_trace_wake_queue);
		wake_up(&rreq->waitq);
	}
}

/*
 * Mark a subrequest as no longer being in progress and, if need be, wake the
 * collector.
 */
void netfs_subreq_clear_in_progress(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	struct netfs_io_stream *stream = &rreq->io_streams[subreq->stream_nr];

	clear_bit_unlock(NETFS_SREQ_IN_PROGRESS, &subreq->flags);
	smp_mb__after_atomic(); /* Clear IN_PROGRESS before task state */

	/* If we are at the head of the queue, wake up the collector. */
	if (list_is_first(&subreq->rreq_link, &stream->subrequests) ||
	    test_bit(NETFS_RREQ_RETRYING, &rreq->flags))
		netfs_wake_collector(rreq);
}

/*
 * Wait for all outstanding I/O in a stream to quiesce.
 */
void netfs_wait_for_in_progress_stream(struct netfs_io_request *rreq,
				       struct netfs_io_stream *stream)
{
	struct netfs_io_subrequest *subreq;
	DEFINE_WAIT(myself);

	list_for_each_entry(subreq, &stream->subrequests, rreq_link) {
		if (!netfs_check_subreq_in_progress(subreq))
			continue;

		trace_netfs_rreq(rreq, netfs_rreq_trace_wait_quiesce);
		for (;;) {
			prepare_to_wait(&rreq->waitq, &myself, TASK_UNINTERRUPTIBLE);

			if (!netfs_check_subreq_in_progress(subreq))
				break;

			trace_netfs_sreq(subreq, netfs_sreq_trace_wait_for);
			schedule();
		}
	}

	trace_netfs_rreq(rreq, netfs_rreq_trace_waited_quiesce);
	finish_wait(&rreq->waitq, &myself);
}

/*
 * Perform collection in app thread if not offloaded to workqueue.
 */
static int netfs_collect_in_app(struct netfs_io_request *rreq,
				bool (*collector)(struct netfs_io_request *rreq))
{
	bool need_collect = false, inactive = true, done = true;

	if (!netfs_check_rreq_in_progress(rreq)) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_recollect);
		return 1; /* Done */
	}

	for (int i = 0; i < NR_IO_STREAMS; i++) {
		struct netfs_io_subrequest *subreq;
		struct netfs_io_stream *stream = &rreq->io_streams[i];

		if (!stream->active)
			continue;
		inactive = false;
		trace_netfs_collect_stream(rreq, stream);
		subreq = list_first_entry_or_null(&stream->subrequests,
						  struct netfs_io_subrequest,
						  rreq_link);
		if (subreq &&
		    (!netfs_check_subreq_in_progress(subreq) ||
		     test_bit(NETFS_SREQ_MADE_PROGRESS, &subreq->flags))) {
			need_collect = true;
			break;
		}
		if (subreq || !test_bit(NETFS_RREQ_ALL_QUEUED, &rreq->flags))
			done = false;
	}

	if (!need_collect && !inactive && !done)
		return 0; /* Sleep */

	__set_current_state(TASK_RUNNING);
	if (collector(rreq)) {
		/* Drop the ref from the NETFS_RREQ_IN_PROGRESS flag. */
		netfs_put_request(rreq, netfs_rreq_trace_put_work_ip);
		return 1; /* Done */
	}

	if (inactive) {
		WARN(true, "Failed to collect inactive req R=%08x\n",
		     rreq->debug_id);
		cond_resched();
	}
	return 2; /* Again */
}

/*
 * Wait for a request to complete, successfully or otherwise.
 */
static ssize_t netfs_wait_for_in_progress(struct netfs_io_request *rreq,
					  bool (*collector)(struct netfs_io_request *rreq))
{
	DEFINE_WAIT(myself);
	ssize_t ret;

	for (;;) {
		prepare_to_wait(&rreq->waitq, &myself, TASK_UNINTERRUPTIBLE);

		if (!test_bit(NETFS_RREQ_OFFLOAD_COLLECTION, &rreq->flags)) {
			switch (netfs_collect_in_app(rreq, collector)) {
			case 0:
				break;
			case 1:
				goto all_collected;
			case 2:
				if (!netfs_check_rreq_in_progress(rreq))
					break;
				cond_resched();
				continue;
			}
		}

		if (!netfs_check_rreq_in_progress(rreq))
			break;

		trace_netfs_rreq(rreq, netfs_rreq_trace_wait_ip);
		schedule();
	}

all_collected:
	trace_netfs_rreq(rreq, netfs_rreq_trace_waited_ip);
	finish_wait(&rreq->waitq, &myself);

	ret = rreq->error;
	if (ret == 0) {
		ret = rreq->transferred;
		switch (rreq->origin) {
		case NETFS_DIO_READ:
		case NETFS_DIO_WRITE:
		case NETFS_READ_SINGLE:
		case NETFS_UNBUFFERED_READ:
		case NETFS_UNBUFFERED_WRITE:
			break;
		default:
			if (rreq->submitted < rreq->len) {
				trace_netfs_failure(rreq, NULL, ret, netfs_fail_short_read);
				ret = -EIO;
			}
			break;
		}
	}

	return ret;
}

ssize_t netfs_wait_for_read(struct netfs_io_request *rreq)
{
	return netfs_wait_for_in_progress(rreq, netfs_read_collection);
}

ssize_t netfs_wait_for_write(struct netfs_io_request *rreq)
{
	return netfs_wait_for_in_progress(rreq, netfs_write_collection);
}

/*
 * Wait for a paused operation to unpause or complete in some manner.
 */
static void netfs_wait_for_pause(struct netfs_io_request *rreq,
				 bool (*collector)(struct netfs_io_request *rreq))
{
	DEFINE_WAIT(myself);

	for (;;) {
		trace_netfs_rreq(rreq, netfs_rreq_trace_wait_pause);
		prepare_to_wait(&rreq->waitq, &myself, TASK_UNINTERRUPTIBLE);

		if (!test_bit(NETFS_RREQ_OFFLOAD_COLLECTION, &rreq->flags)) {
			switch (netfs_collect_in_app(rreq, collector)) {
			case 0:
				break;
			case 1:
				goto all_collected;
			case 2:
				if (!netfs_check_rreq_in_progress(rreq) ||
				    !test_bit(NETFS_RREQ_PAUSE, &rreq->flags))
					break;
				cond_resched();
				continue;
			}
		}

		if (!netfs_check_rreq_in_progress(rreq) ||
		    !test_bit(NETFS_RREQ_PAUSE, &rreq->flags))
			break;

		schedule();
	}

all_collected:
	trace_netfs_rreq(rreq, netfs_rreq_trace_waited_pause);
	finish_wait(&rreq->waitq, &myself);
}

void netfs_wait_for_paused_read(struct netfs_io_request *rreq)
{
	return netfs_wait_for_pause(rreq, netfs_read_collection);
}

void netfs_wait_for_paused_write(struct netfs_io_request *rreq)
{
	return netfs_wait_for_pause(rreq, netfs_write_collection);
}
