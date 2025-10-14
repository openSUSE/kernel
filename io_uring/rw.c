// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/blk-mq.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fsnotify.h>
#include <linux/poll.h>
#include <linux/nospec.h>
#include <linux/compat.h>
#include <linux/io_uring/cmd.h>
#include <linux/indirect_call_wrapper.h>

#include <uapi/linux/io_uring.h>

#include "filetable.h"
#include "io_uring.h"
#include "opdef.h"
#include "kbuf.h"
#include "alloc_cache.h"
#include "rsrc.h"
#include "poll.h"
#include "rw.h"

static void io_complete_rw(struct kiocb *kiocb, long res);
static void io_complete_rw_iopoll(struct kiocb *kiocb, long res);

struct io_rw {
	/* NOTE: kiocb has the file as the first member, so don't do it here */
	struct kiocb			kiocb;
	u64				addr;
	u32				len;
	rwf_t				flags;
};

static bool io_file_supports_nowait(struct io_kiocb *req, __poll_t mask)
{
	/* If FMODE_NOWAIT is set for a file, we're golden */
	if (req->flags & REQ_F_SUPPORT_NOWAIT)
		return true;
	/* No FMODE_NOWAIT, if we can poll, check the status */
	if (io_file_can_poll(req)) {
		struct poll_table_struct pt = { ._key = mask };

		return vfs_poll(req->file, &pt) & mask;
	}
	/* No FMODE_NOWAIT support, and file isn't pollable. Tough luck. */
	return false;
}

static int io_iov_compat_buffer_select_prep(struct io_rw *rw)
{
	struct compat_iovec __user *uiov = u64_to_user_ptr(rw->addr);
	struct compat_iovec iov;

	if (copy_from_user(&iov, uiov, sizeof(iov)))
		return -EFAULT;
	rw->len = iov.iov_len;
	return 0;
}

static int io_iov_buffer_select_prep(struct io_kiocb *req)
{
	struct iovec __user *uiov;
	struct iovec iov;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	if (rw->len != 1)
		return -EINVAL;

	if (io_is_compat(req->ctx))
		return io_iov_compat_buffer_select_prep(rw);

	uiov = u64_to_user_ptr(rw->addr);
	if (copy_from_user(&iov, uiov, sizeof(*uiov)))
		return -EFAULT;
	rw->len = iov.iov_len;
	return 0;
}

static int io_import_vec(int ddir, struct io_kiocb *req,
			 struct io_async_rw *io,
			 const struct iovec __user *uvec,
			 size_t uvec_segs)
{
	int ret, nr_segs;
	struct iovec *iov;

	if (io->vec.iovec) {
		nr_segs = io->vec.nr;
		iov = io->vec.iovec;
	} else {
		nr_segs = 1;
		iov = &io->fast_iov;
	}

	ret = __import_iovec(ddir, uvec, uvec_segs, nr_segs, &iov, &io->iter,
			     io_is_compat(req->ctx));
	if (unlikely(ret < 0))
		return ret;
	if (iov) {
		req->flags |= REQ_F_NEED_CLEANUP;
		io_vec_reset_iovec(&io->vec, iov, io->iter.nr_segs);
	}
	return 0;
}

static int __io_import_rw_buffer(int ddir, struct io_kiocb *req,
				 struct io_async_rw *io, struct io_br_sel *sel,
				 unsigned int issue_flags)
{
	const struct io_issue_def *def = &io_issue_defs[req->opcode];
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	size_t sqe_len = rw->len;

	sel->addr = u64_to_user_ptr(rw->addr);
	if (def->vectored && !(req->flags & REQ_F_BUFFER_SELECT))
		return io_import_vec(ddir, req, io, sel->addr, sqe_len);

	if (io_do_buffer_select(req)) {
		*sel = io_buffer_select(req, &sqe_len, io->buf_group, issue_flags);
		if (!sel->addr)
			return -ENOBUFS;
		rw->addr = (unsigned long) sel->addr;
		rw->len = sqe_len;
	}
	return import_ubuf(ddir, sel->addr, sqe_len, &io->iter);
}

static inline int io_import_rw_buffer(int rw, struct io_kiocb *req,
				      struct io_async_rw *io,
				      struct io_br_sel *sel,
				      unsigned int issue_flags)
{
	int ret;

	ret = __io_import_rw_buffer(rw, req, io, sel, issue_flags);
	if (unlikely(ret < 0))
		return ret;

	iov_iter_save_state(&io->iter, &io->iter_state);
	return 0;
}

static void io_rw_recycle(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_async_rw *rw = req->async_data;

	if (unlikely(issue_flags & IO_URING_F_UNLOCKED))
		return;

	io_alloc_cache_vec_kasan(&rw->vec);
	if (rw->vec.nr > IO_VEC_CACHE_SOFT_CAP)
		io_vec_free(&rw->vec);

	if (io_alloc_cache_put(&req->ctx->rw_cache, rw))
		io_req_async_data_clear(req, 0);
}

static void io_req_rw_cleanup(struct io_kiocb *req, unsigned int issue_flags)
{
	/*
	 * Disable quick recycling for anything that's gone through io-wq.
	 * In theory, this should be fine to cleanup. However, some read or
	 * write iter handling touches the iovec AFTER having called into the
	 * handler, eg to reexpand or revert. This means we can have:
	 *
	 * task			io-wq
	 *   issue
	 *     punt to io-wq
	 *			issue
	 *			  blkdev_write_iter()
	 *			    ->ki_complete()
	 *			      io_complete_rw()
	 *			        queue tw complete
	 *  run tw
	 *    req_rw_cleanup
	 *			iov_iter_count() <- look at iov_iter again
	 *
	 * which can lead to a UAF. This is only possible for io-wq offload
	 * as the cleanup can run in parallel. As io-wq is not the fast path,
	 * just leave cleanup to the end.
	 *
	 * This is really a bug in the core code that does this, any issue
	 * path should assume that a successful (or -EIOCBQUEUED) return can
	 * mean that the underlying data can be gone at any time. But that
	 * should be fixed seperately, and then this check could be killed.
	 */
	if (!(req->flags & (REQ_F_REISSUE | REQ_F_REFCOUNT))) {
		req->flags &= ~REQ_F_NEED_CLEANUP;
		io_rw_recycle(req, issue_flags);
	}
}

static int io_rw_alloc_async(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_async_rw *rw;

	rw = io_uring_alloc_async_data(&ctx->rw_cache, req);
	if (!rw)
		return -ENOMEM;
	if (rw->vec.iovec)
		req->flags |= REQ_F_NEED_CLEANUP;
	rw->bytes_done = 0;
	return 0;
}

static inline void io_meta_save_state(struct io_async_rw *io)
{
	io->meta_state.seed = io->meta.seed;
	iov_iter_save_state(&io->meta.iter, &io->meta_state.iter_meta);
}

static inline void io_meta_restore(struct io_async_rw *io, struct kiocb *kiocb)
{
	if (kiocb->ki_flags & IOCB_HAS_METADATA) {
		io->meta.seed = io->meta_state.seed;
		iov_iter_restore(&io->meta.iter, &io->meta_state.iter_meta);
	}
}

static int io_prep_rw_pi(struct io_kiocb *req, struct io_rw *rw, int ddir,
			 u64 attr_ptr, u64 attr_type_mask)
{
	struct io_uring_attr_pi pi_attr;
	struct io_async_rw *io;
	int ret;

	if (copy_from_user(&pi_attr, u64_to_user_ptr(attr_ptr),
	    sizeof(pi_attr)))
		return -EFAULT;

	if (pi_attr.rsvd)
		return -EINVAL;

	io = req->async_data;
	io->meta.flags = pi_attr.flags;
	io->meta.app_tag = pi_attr.app_tag;
	io->meta.seed = pi_attr.seed;
	ret = import_ubuf(ddir, u64_to_user_ptr(pi_attr.addr),
			  pi_attr.len, &io->meta.iter);
	if (unlikely(ret < 0))
		return ret;
	req->flags |= REQ_F_HAS_METADATA;
	io_meta_save_state(io);
	return ret;
}

static int __io_prep_rw(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			int ddir)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_async_rw *io;
	unsigned ioprio;
	u64 attr_type_mask;
	int ret;

	if (io_rw_alloc_async(req))
		return -ENOMEM;
	io = req->async_data;

	rw->kiocb.ki_pos = READ_ONCE(sqe->off);
	/* used for fixed read/write too - just read unconditionally */
	req->buf_index = READ_ONCE(sqe->buf_index);
	io->buf_group = req->buf_index;

	ioprio = READ_ONCE(sqe->ioprio);
	if (ioprio) {
		ret = ioprio_check_cap(ioprio);
		if (ret)
			return ret;

		rw->kiocb.ki_ioprio = ioprio;
	} else {
		rw->kiocb.ki_ioprio = get_current_ioprio();
	}
	rw->kiocb.dio_complete = NULL;
	rw->kiocb.ki_flags = 0;
	rw->kiocb.ki_write_stream = READ_ONCE(sqe->write_stream);

	if (req->ctx->flags & IORING_SETUP_IOPOLL)
		rw->kiocb.ki_complete = io_complete_rw_iopoll;
	else
		rw->kiocb.ki_complete = io_complete_rw;

	rw->addr = READ_ONCE(sqe->addr);
	rw->len = READ_ONCE(sqe->len);
	rw->flags = (__force rwf_t) READ_ONCE(sqe->rw_flags);

	attr_type_mask = READ_ONCE(sqe->attr_type_mask);
	if (attr_type_mask) {
		u64 attr_ptr;

		/* only PI attribute is supported currently */
		if (attr_type_mask != IORING_RW_ATTR_FLAG_PI)
			return -EINVAL;

		attr_ptr = READ_ONCE(sqe->attr_ptr);
		return io_prep_rw_pi(req, rw, ddir, attr_ptr, attr_type_mask);
	}
	return 0;
}

static int io_rw_do_import(struct io_kiocb *req, int ddir)
{
	struct io_br_sel sel = { };

	if (io_do_buffer_select(req))
		return 0;

	return io_import_rw_buffer(ddir, req, req->async_data, &sel, 0);
}

static int io_prep_rw(struct io_kiocb *req, const struct io_uring_sqe *sqe,
		      int ddir)
{
	int ret;

	ret = __io_prep_rw(req, sqe, ddir);
	if (unlikely(ret))
		return ret;

	return io_rw_do_import(req, ddir);
}

int io_prep_read(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return io_prep_rw(req, sqe, ITER_DEST);
}

int io_prep_write(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return io_prep_rw(req, sqe, ITER_SOURCE);
}

static int io_prep_rwv(struct io_kiocb *req, const struct io_uring_sqe *sqe,
		       int ddir)
{
	int ret;

	ret = io_prep_rw(req, sqe, ddir);
	if (unlikely(ret))
		return ret;
	if (!(req->flags & REQ_F_BUFFER_SELECT))
		return 0;

	/*
	 * Have to do this validation here, as this is in io_read() rw->len
	 * might have chanaged due to buffer selection
	 */
	return io_iov_buffer_select_prep(req);
}

int io_prep_readv(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return io_prep_rwv(req, sqe, ITER_DEST);
}

int io_prep_writev(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return io_prep_rwv(req, sqe, ITER_SOURCE);
}

static int io_init_rw_fixed(struct io_kiocb *req, unsigned int issue_flags,
			    int ddir)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_async_rw *io = req->async_data;
	int ret;

	if (io->bytes_done)
		return 0;

	ret = io_import_reg_buf(req, &io->iter, rw->addr, rw->len, ddir,
				issue_flags);
	iov_iter_save_state(&io->iter, &io->iter_state);
	return ret;
}

int io_prep_read_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return __io_prep_rw(req, sqe, ITER_DEST);
}

int io_prep_write_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return __io_prep_rw(req, sqe, ITER_SOURCE);
}

static int io_rw_import_reg_vec(struct io_kiocb *req,
				struct io_async_rw *io,
				int ddir, unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	unsigned uvec_segs = rw->len;
	int ret;

	ret = io_import_reg_vec(ddir, &io->iter, req, &io->vec,
				uvec_segs, issue_flags);
	if (unlikely(ret))
		return ret;
	iov_iter_save_state(&io->iter, &io->iter_state);
	req->flags &= ~REQ_F_IMPORT_BUFFER;
	return 0;
}

static int io_rw_prep_reg_vec(struct io_kiocb *req)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_async_rw *io = req->async_data;
	const struct iovec __user *uvec;

	uvec = u64_to_user_ptr(rw->addr);
	return io_prep_reg_iovec(req, &io->vec, uvec, rw->len);
}

int io_prep_readv_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	int ret;

	ret = __io_prep_rw(req, sqe, ITER_DEST);
	if (unlikely(ret))
		return ret;
	return io_rw_prep_reg_vec(req);
}

int io_prep_writev_fixed(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	int ret;

	ret = __io_prep_rw(req, sqe, ITER_SOURCE);
	if (unlikely(ret))
		return ret;
	return io_rw_prep_reg_vec(req);
}

/*
 * Multishot read is prepared just like a normal read/write request, only
 * difference is that we set the MULTISHOT flag.
 */
int io_read_mshot_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	int ret;

	/* must be used with provided buffers */
	if (!(req->flags & REQ_F_BUFFER_SELECT))
		return -EINVAL;

	ret = __io_prep_rw(req, sqe, ITER_DEST);
	if (unlikely(ret))
		return ret;

	if (rw->addr || rw->len)
		return -EINVAL;

	req->flags |= REQ_F_APOLL_MULTISHOT;
	return 0;
}

void io_readv_writev_cleanup(struct io_kiocb *req)
{
	lockdep_assert_held(&req->ctx->uring_lock);
	io_rw_recycle(req, 0);
}

static inline loff_t *io_kiocb_update_pos(struct io_kiocb *req)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	if (rw->kiocb.ki_pos != -1)
		return &rw->kiocb.ki_pos;

	if (!(req->file->f_mode & FMODE_STREAM)) {
		req->flags |= REQ_F_CUR_POS;
		rw->kiocb.ki_pos = req->file->f_pos;
		return &rw->kiocb.ki_pos;
	}

	rw->kiocb.ki_pos = 0;
	return NULL;
}

static bool io_rw_should_reissue(struct io_kiocb *req)
{
#ifdef CONFIG_BLOCK
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	umode_t mode = file_inode(req->file)->i_mode;
	struct io_async_rw *io = req->async_data;
	struct io_ring_ctx *ctx = req->ctx;

	if (!S_ISBLK(mode) && !S_ISREG(mode))
		return false;
	if ((req->flags & REQ_F_NOWAIT) || (io_wq_current_is_worker() &&
	    !(ctx->flags & IORING_SETUP_IOPOLL)))
		return false;
	/*
	 * If ref is dying, we might be running poll reap from the exit work.
	 * Don't attempt to reissue from that path, just let it fail with
	 * -EAGAIN.
	 */
	if (percpu_ref_is_dying(&ctx->refs))
		return false;

	io_meta_restore(io, &rw->kiocb);
	iov_iter_restore(&io->iter, &io->iter_state);
	return true;
#else
	return false;
#endif
}

static void io_req_end_write(struct io_kiocb *req)
{
	if (req->flags & REQ_F_ISREG) {
		struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

		kiocb_end_write(&rw->kiocb);
	}
}

/*
 * Trigger the notifications after having done some IO, and finish the write
 * accounting, if any.
 */
static void io_req_io_end(struct io_kiocb *req)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	if (rw->kiocb.ki_flags & IOCB_WRITE) {
		io_req_end_write(req);
		fsnotify_modify(req->file);
	} else {
		fsnotify_access(req->file);
	}
}

static void __io_complete_rw_common(struct io_kiocb *req, long res)
{
	if (res == req->cqe.res)
		return;
	if (res == -EAGAIN && io_rw_should_reissue(req)) {
		req->flags |= REQ_F_REISSUE | REQ_F_BL_NO_RECYCLE;
	} else {
		req_set_fail(req);
		req->cqe.res = res;
	}
}

static inline int io_fixup_rw_res(struct io_kiocb *req, long res)
{
	struct io_async_rw *io = req->async_data;

	/* add previously done IO, if any */
	if (req_has_async_data(req) && io->bytes_done > 0) {
		if (res < 0)
			res = io->bytes_done;
		else
			res += io->bytes_done;
	}
	return res;
}

void io_req_rw_complete(struct io_kiocb *req, io_tw_token_t tw)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct kiocb *kiocb = &rw->kiocb;

	if ((kiocb->ki_flags & IOCB_DIO_CALLER_COMP) && kiocb->dio_complete) {
		long res = kiocb->dio_complete(rw->kiocb.private);

		io_req_set_res(req, io_fixup_rw_res(req, res), 0);
	}

	io_req_io_end(req);

	if (req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING))
		req->cqe.flags |= io_put_kbuf(req, req->cqe.res, NULL);

	io_req_rw_cleanup(req, 0);
	io_req_task_complete(req, tw);
}

static void io_complete_rw(struct kiocb *kiocb, long res)
{
	struct io_rw *rw = container_of(kiocb, struct io_rw, kiocb);
	struct io_kiocb *req = cmd_to_io_kiocb(rw);

	if (!kiocb->dio_complete || !(kiocb->ki_flags & IOCB_DIO_CALLER_COMP)) {
		__io_complete_rw_common(req, res);
		io_req_set_res(req, io_fixup_rw_res(req, res), 0);
	}
	req->io_task_work.func = io_req_rw_complete;
	__io_req_task_work_add(req, IOU_F_TWQ_LAZY_WAKE);
}

static void io_complete_rw_iopoll(struct kiocb *kiocb, long res)
{
	struct io_rw *rw = container_of(kiocb, struct io_rw, kiocb);
	struct io_kiocb *req = cmd_to_io_kiocb(rw);

	if (kiocb->ki_flags & IOCB_WRITE)
		io_req_end_write(req);
	if (unlikely(res != req->cqe.res)) {
		if (res == -EAGAIN && io_rw_should_reissue(req))
			req->flags |= REQ_F_REISSUE | REQ_F_BL_NO_RECYCLE;
		else
			req->cqe.res = res;
	}

	/* order with io_iopoll_complete() checking ->iopoll_completed */
	smp_store_release(&req->iopoll_completed, 1);
}

static inline void io_rw_done(struct io_kiocb *req, ssize_t ret)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	/* IO was queued async, completion will happen later */
	if (ret == -EIOCBQUEUED)
		return;

	/* transform internal restart error codes */
	if (unlikely(ret < 0)) {
		switch (ret) {
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
		case -ERESTARTNOHAND:
		case -ERESTART_RESTARTBLOCK:
			/*
			 * We can't just restart the syscall, since previously
			 * submitted sqes may already be in progress. Just fail
			 * this IO with EINTR.
			 */
			ret = -EINTR;
			break;
		}
	}

	if (req->ctx->flags & IORING_SETUP_IOPOLL)
		io_complete_rw_iopoll(&rw->kiocb, ret);
	else
		io_complete_rw(&rw->kiocb, ret);
}

static int kiocb_done(struct io_kiocb *req, ssize_t ret,
		      struct io_br_sel *sel, unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	unsigned final_ret = io_fixup_rw_res(req, ret);

	if (ret >= 0 && req->flags & REQ_F_CUR_POS)
		req->file->f_pos = rw->kiocb.ki_pos;
	if (ret >= 0 && !(req->ctx->flags & IORING_SETUP_IOPOLL)) {
		__io_complete_rw_common(req, ret);
		/*
		 * Safe to call io_end from here as we're inline
		 * from the submission path.
		 */
		io_req_io_end(req);
		io_req_set_res(req, final_ret, io_put_kbuf(req, ret, sel->buf_list));
		io_req_rw_cleanup(req, issue_flags);
		return IOU_COMPLETE;
	} else {
		io_rw_done(req, ret);
	}

	return IOU_ISSUE_SKIP_COMPLETE;
}

static inline loff_t *io_kiocb_ppos(struct kiocb *kiocb)
{
	return (kiocb->ki_filp->f_mode & FMODE_STREAM) ? NULL : &kiocb->ki_pos;
}

/*
 * For files that don't have ->read_iter() and ->write_iter(), handle them
 * by looping over ->read() or ->write() manually.
 */
static ssize_t loop_rw_iter(int ddir, struct io_rw *rw, struct iov_iter *iter)
{
	struct io_kiocb *req = cmd_to_io_kiocb(rw);
	struct kiocb *kiocb = &rw->kiocb;
	struct file *file = kiocb->ki_filp;
	ssize_t ret = 0;
	loff_t *ppos;

	/*
	 * Don't support polled IO through this interface, and we can't
	 * support non-blocking either. For the latter, this just causes
	 * the kiocb to be handled from an async context.
	 */
	if (kiocb->ki_flags & IOCB_HIPRI)
		return -EOPNOTSUPP;
	if ((kiocb->ki_flags & IOCB_NOWAIT) &&
	    !(kiocb->ki_filp->f_flags & O_NONBLOCK))
		return -EAGAIN;
	if ((req->flags & REQ_F_BUF_NODE) && req->buf_node->buf->is_kbuf)
		return -EFAULT;

	ppos = io_kiocb_ppos(kiocb);

	while (iov_iter_count(iter)) {
		void __user *addr;
		size_t len;
		ssize_t nr;

		if (iter_is_ubuf(iter)) {
			addr = iter->ubuf + iter->iov_offset;
			len = iov_iter_count(iter);
		} else if (!iov_iter_is_bvec(iter)) {
			addr = iter_iov_addr(iter);
			len = iter_iov_len(iter);
		} else {
			addr = u64_to_user_ptr(rw->addr);
			len = rw->len;
		}

		if (ddir == READ)
			nr = file->f_op->read(file, addr, len, ppos);
		else
			nr = file->f_op->write(file, addr, len, ppos);

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (!iov_iter_is_bvec(iter)) {
			iov_iter_advance(iter, nr);
		} else {
			rw->addr += nr;
			rw->len -= nr;
			if (!rw->len)
				break;
		}
		if (nr != len)
			break;
	}

	return ret;
}

/*
 * This is our waitqueue callback handler, registered through __folio_lock_async()
 * when we initially tried to do the IO with the iocb armed our waitqueue.
 * This gets called when the page is unlocked, and we generally expect that to
 * happen when the page IO is completed and the page is now uptodate. This will
 * queue a task_work based retry of the operation, attempting to copy the data
 * again. If the latter fails because the page was NOT uptodate, then we will
 * do a thread based blocking retry of the operation. That's the unexpected
 * slow path.
 */
static int io_async_buf_func(struct wait_queue_entry *wait, unsigned mode,
			     int sync, void *arg)
{
	struct wait_page_queue *wpq;
	struct io_kiocb *req = wait->private;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct wait_page_key *key = arg;

	wpq = container_of(wait, struct wait_page_queue, wait);

	if (!wake_page_match(wpq, key))
		return 0;

	rw->kiocb.ki_flags &= ~IOCB_WAITQ;
	list_del_init(&wait->entry);
	io_req_task_queue(req);
	return 1;
}

/*
 * This controls whether a given IO request should be armed for async page
 * based retry. If we return false here, the request is handed to the async
 * worker threads for retry. If we're doing buffered reads on a regular file,
 * we prepare a private wait_page_queue entry and retry the operation. This
 * will either succeed because the page is now uptodate and unlocked, or it
 * will register a callback when the page is unlocked at IO completion. Through
 * that callback, io_uring uses task_work to setup a retry of the operation.
 * That retry will attempt the buffered read again. The retry will generally
 * succeed, or in rare cases where it fails, we then fall back to using the
 * async worker threads for a blocking retry.
 */
static bool io_rw_should_retry(struct io_kiocb *req)
{
	struct io_async_rw *io = req->async_data;
	struct wait_page_queue *wait = &io->wpq;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct kiocb *kiocb = &rw->kiocb;

	/*
	 * Never retry for NOWAIT or a request with metadata, we just complete
	 * with -EAGAIN.
	 */
	if (req->flags & (REQ_F_NOWAIT | REQ_F_HAS_METADATA))
		return false;

	/* Only for buffered IO */
	if (kiocb->ki_flags & (IOCB_DIRECT | IOCB_HIPRI))
		return false;

	/*
	 * just use poll if we can, and don't attempt if the fs doesn't
	 * support callback based unlocks
	 */
	if (io_file_can_poll(req) ||
	    !(req->file->f_op->fop_flags & FOP_BUFFER_RASYNC))
		return false;

	wait->wait.func = io_async_buf_func;
	wait->wait.private = req;
	wait->wait.flags = 0;
	INIT_LIST_HEAD(&wait->wait.entry);
	kiocb->ki_flags |= IOCB_WAITQ;
	kiocb->ki_flags &= ~IOCB_NOWAIT;
	kiocb->ki_waitq = wait;
	return true;
}

static inline int io_iter_do_read(struct io_rw *rw, struct iov_iter *iter)
{
	struct file *file = rw->kiocb.ki_filp;

	if (likely(file->f_op->read_iter))
		return file->f_op->read_iter(&rw->kiocb, iter);
	else if (file->f_op->read)
		return loop_rw_iter(READ, rw, iter);
	else
		return -EINVAL;
}

static bool need_complete_io(struct io_kiocb *req)
{
	return req->flags & REQ_F_ISREG ||
		S_ISBLK(file_inode(req->file)->i_mode);
}

static int io_rw_init_file(struct io_kiocb *req, fmode_t mode, int rw_type)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct kiocb *kiocb = &rw->kiocb;
	struct io_ring_ctx *ctx = req->ctx;
	struct file *file = req->file;
	int ret;

	if (unlikely(!(file->f_mode & mode)))
		return -EBADF;

	if (!(req->flags & REQ_F_FIXED_FILE))
		req->flags |= io_file_get_flags(file);

	kiocb->ki_flags = file->f_iocb_flags;
	ret = kiocb_set_rw_flags(kiocb, rw->flags, rw_type);
	if (unlikely(ret))
		return ret;
	kiocb->ki_flags |= IOCB_ALLOC_CACHE;

	/*
	 * If the file is marked O_NONBLOCK, still allow retry for it if it
	 * supports async. Otherwise it's impossible to use O_NONBLOCK files
	 * reliably. If not, or it IOCB_NOWAIT is set, don't retry.
	 */
	if (kiocb->ki_flags & IOCB_NOWAIT ||
	    ((file->f_flags & O_NONBLOCK && !(req->flags & REQ_F_SUPPORT_NOWAIT))))
		req->flags |= REQ_F_NOWAIT;

	if (ctx->flags & IORING_SETUP_IOPOLL) {
		if (!(kiocb->ki_flags & IOCB_DIRECT) || !file->f_op->iopoll)
			return -EOPNOTSUPP;
		kiocb->private = NULL;
		kiocb->ki_flags |= IOCB_HIPRI;
		req->iopoll_completed = 0;
		if (ctx->flags & IORING_SETUP_HYBRID_IOPOLL) {
			/* make sure every req only blocks once*/
			req->flags &= ~REQ_F_IOPOLL_STATE;
			req->iopoll_start = ktime_get_ns();
		}
	} else {
		if (kiocb->ki_flags & IOCB_HIPRI)
			return -EINVAL;
	}

	if (req->flags & REQ_F_HAS_METADATA) {
		struct io_async_rw *io = req->async_data;

		if (!(file->f_mode & FMODE_HAS_METADATA))
			return -EINVAL;

		/*
		 * We have a union of meta fields with wpq used for buffered-io
		 * in io_async_rw, so fail it here.
		 */
		if (!(req->file->f_flags & O_DIRECT))
			return -EOPNOTSUPP;
		kiocb->ki_flags |= IOCB_HAS_METADATA;
		kiocb->private = &io->meta;
	}

	return 0;
}

static int __io_read(struct io_kiocb *req, struct io_br_sel *sel,
		     unsigned int issue_flags)
{
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_async_rw *io = req->async_data;
	struct kiocb *kiocb = &rw->kiocb;
	ssize_t ret;
	loff_t *ppos;

	if (req->flags & REQ_F_IMPORT_BUFFER) {
		ret = io_rw_import_reg_vec(req, io, ITER_DEST, issue_flags);
		if (unlikely(ret))
			return ret;
	} else if (io_do_buffer_select(req)) {
		ret = io_import_rw_buffer(ITER_DEST, req, io, sel, issue_flags);
		if (unlikely(ret < 0))
			return ret;
	}
	ret = io_rw_init_file(req, FMODE_READ, READ);
	if (unlikely(ret))
		return ret;
	req->cqe.res = iov_iter_count(&io->iter);

	if (force_nonblock) {
		/* If the file doesn't support async, just async punt */
		if (unlikely(!io_file_supports_nowait(req, EPOLLIN)))
			return -EAGAIN;
		kiocb->ki_flags |= IOCB_NOWAIT;
	} else {
		/* Ensure we clear previously set non-block flag */
		kiocb->ki_flags &= ~IOCB_NOWAIT;
	}

	ppos = io_kiocb_update_pos(req);

	ret = rw_verify_area(READ, req->file, ppos, req->cqe.res);
	if (unlikely(ret))
		return ret;

	ret = io_iter_do_read(rw, &io->iter);

	/*
	 * Some file systems like to return -EOPNOTSUPP for an IOCB_NOWAIT
	 * issue, even though they should be returning -EAGAIN. To be safe,
	 * retry from blocking context for either.
	 */
	if (ret == -EOPNOTSUPP && force_nonblock)
		ret = -EAGAIN;

	if (ret == -EAGAIN) {
		/* If we can poll, just do that. */
		if (io_file_can_poll(req))
			return -EAGAIN;
		/* IOPOLL retry should happen for io-wq threads */
		if (!force_nonblock && !(req->ctx->flags & IORING_SETUP_IOPOLL))
			goto done;
		/* no retry on NONBLOCK nor RWF_NOWAIT */
		if (req->flags & REQ_F_NOWAIT)
			goto done;
		ret = 0;
	} else if (ret == -EIOCBQUEUED) {
		return IOU_ISSUE_SKIP_COMPLETE;
	} else if (ret == req->cqe.res || ret <= 0 || !force_nonblock ||
		   (req->flags & REQ_F_NOWAIT) || !need_complete_io(req) ||
		   (issue_flags & IO_URING_F_MULTISHOT)) {
		/* read all, failed, already did sync or don't want to retry */
		goto done;
	}

	/*
	 * Don't depend on the iter state matching what was consumed, or being
	 * untouched in case of error. Restore it and we'll advance it
	 * manually if we need to.
	 */
	iov_iter_restore(&io->iter, &io->iter_state);
	io_meta_restore(io, kiocb);

	do {
		/*
		 * We end up here because of a partial read, either from
		 * above or inside this loop. Advance the iter by the bytes
		 * that were consumed.
		 */
		iov_iter_advance(&io->iter, ret);
		if (!iov_iter_count(&io->iter))
			break;
		io->bytes_done += ret;
		iov_iter_save_state(&io->iter, &io->iter_state);

		/* if we can retry, do so with the callbacks armed */
		if (!io_rw_should_retry(req)) {
			kiocb->ki_flags &= ~IOCB_WAITQ;
			return -EAGAIN;
		}

		req->cqe.res = iov_iter_count(&io->iter);
		/*
		 * Now retry read with the IOCB_WAITQ parts set in the iocb. If
		 * we get -EIOCBQUEUED, then we'll get a notification when the
		 * desired page gets unlocked. We can also get a partial read
		 * here, and if we do, then just retry at the new offset.
		 */
		ret = io_iter_do_read(rw, &io->iter);
		if (ret == -EIOCBQUEUED)
			return IOU_ISSUE_SKIP_COMPLETE;
		/* we got some bytes, but not all. retry. */
		kiocb->ki_flags &= ~IOCB_WAITQ;
		iov_iter_restore(&io->iter, &io->iter_state);
	} while (ret > 0);
done:
	/* it's faster to check here then delegate to kfree */
	return ret;
}

int io_read(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_br_sel sel = { };
	int ret;

	ret = __io_read(req, &sel, issue_flags);
	if (ret >= 0)
		return kiocb_done(req, ret, &sel, issue_flags);

	if (req->flags & REQ_F_BUFFERS_COMMIT)
		io_kbuf_recycle(req, sel.buf_list, issue_flags);
	return ret;
}

int io_read_mshot(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_br_sel sel = { };
	unsigned int cflags = 0;
	int ret;

	/*
	 * Multishot MUST be used on a pollable file
	 */
	if (!io_file_can_poll(req))
		return -EBADFD;

	/* make it sync, multishot doesn't support async execution */
	rw->kiocb.ki_complete = NULL;
	ret = __io_read(req, &sel, issue_flags);

	/*
	 * If we get -EAGAIN, recycle our buffer and just let normal poll
	 * handling arm it.
	 */
	if (ret == -EAGAIN) {
		/*
		 * Reset rw->len to 0 again to avoid clamping future mshot
		 * reads, in case the buffer size varies.
		 */
		if (io_kbuf_recycle(req, sel.buf_list, issue_flags))
			rw->len = 0;
		return IOU_RETRY;
	} else if (ret <= 0) {
		io_kbuf_recycle(req, sel.buf_list, issue_flags);
		if (ret < 0)
			req_set_fail(req);
	} else if (!(req->flags & REQ_F_APOLL_MULTISHOT)) {
		cflags = io_put_kbuf(req, ret, sel.buf_list);
	} else {
		/*
		 * Any successful return value will keep the multishot read
		 * armed, if it's still set. Put our buffer and post a CQE. If
		 * we fail to post a CQE, or multishot is no longer set, then
		 * jump to the termination path. This request is then done.
		 */
		cflags = io_put_kbuf(req, ret, sel.buf_list);
		rw->len = 0; /* similarly to above, reset len to 0 */

		if (io_req_post_cqe(req, ret, cflags | IORING_CQE_F_MORE)) {
			if (issue_flags & IO_URING_F_MULTISHOT)
				/*
				 * Force retry, as we might have more data to
				 * be read and otherwise it won't get retried
				 * until (if ever) another poll is triggered.
				 */
				io_poll_multishot_retry(req);

			return IOU_RETRY;
		}
	}

	/*
	 * Either an error, or we've hit overflow posting the CQE. For any
	 * multishot request, hitting overflow will terminate it.
	 */
	io_req_set_res(req, ret, cflags);
	io_req_rw_cleanup(req, issue_flags);
	return IOU_COMPLETE;
}

static bool io_kiocb_start_write(struct io_kiocb *req, struct kiocb *kiocb)
{
	struct inode *inode;
	bool ret;

	if (!(req->flags & REQ_F_ISREG))
		return true;
	if (!(kiocb->ki_flags & IOCB_NOWAIT)) {
		kiocb_start_write(kiocb);
		return true;
	}

	inode = file_inode(kiocb->ki_filp);
	ret = sb_start_write_trylock(inode->i_sb);
	if (ret)
		__sb_writers_release(inode->i_sb, SB_FREEZE_WRITE);
	return ret;
}

int io_write(struct io_kiocb *req, unsigned int issue_flags)
{
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_async_rw *io = req->async_data;
	struct kiocb *kiocb = &rw->kiocb;
	ssize_t ret, ret2;
	loff_t *ppos;

	if (req->flags & REQ_F_IMPORT_BUFFER) {
		ret = io_rw_import_reg_vec(req, io, ITER_SOURCE, issue_flags);
		if (unlikely(ret))
			return ret;
	}

	ret = io_rw_init_file(req, FMODE_WRITE, WRITE);
	if (unlikely(ret))
		return ret;
	req->cqe.res = iov_iter_count(&io->iter);

	if (force_nonblock) {
		/* If the file doesn't support async, just async punt */
		if (unlikely(!io_file_supports_nowait(req, EPOLLOUT)))
			goto ret_eagain;

		/* Check if we can support NOWAIT. */
		if (!(kiocb->ki_flags & IOCB_DIRECT) &&
		    !(req->file->f_op->fop_flags & FOP_BUFFER_WASYNC) &&
		    (req->flags & REQ_F_ISREG))
			goto ret_eagain;

		kiocb->ki_flags |= IOCB_NOWAIT;
	} else {
		/* Ensure we clear previously set non-block flag */
		kiocb->ki_flags &= ~IOCB_NOWAIT;
	}

	ppos = io_kiocb_update_pos(req);

	ret = rw_verify_area(WRITE, req->file, ppos, req->cqe.res);
	if (unlikely(ret))
		return ret;

	if (unlikely(!io_kiocb_start_write(req, kiocb)))
		return -EAGAIN;
	kiocb->ki_flags |= IOCB_WRITE;

	if (likely(req->file->f_op->write_iter))
		ret2 = req->file->f_op->write_iter(kiocb, &io->iter);
	else if (req->file->f_op->write)
		ret2 = loop_rw_iter(WRITE, rw, &io->iter);
	else
		ret2 = -EINVAL;

	/*
	 * Raw bdev writes will return -EOPNOTSUPP for IOCB_NOWAIT. Just
	 * retry them without IOCB_NOWAIT.
	 */
	if (ret2 == -EOPNOTSUPP && (kiocb->ki_flags & IOCB_NOWAIT))
		ret2 = -EAGAIN;
	/* no retry on NONBLOCK nor RWF_NOWAIT */
	if (ret2 == -EAGAIN && (req->flags & REQ_F_NOWAIT))
		goto done;
	if (!force_nonblock || ret2 != -EAGAIN) {
		/* IOPOLL retry should happen for io-wq threads */
		if (ret2 == -EAGAIN && (req->ctx->flags & IORING_SETUP_IOPOLL))
			goto ret_eagain;

		if (ret2 != req->cqe.res && ret2 >= 0 && need_complete_io(req)) {
			trace_io_uring_short_write(req->ctx, kiocb->ki_pos - ret2,
						req->cqe.res, ret2);

			/* This is a partial write. The file pos has already been
			 * updated, setup the async struct to complete the request
			 * in the worker. Also update bytes_done to account for
			 * the bytes already written.
			 */
			iov_iter_save_state(&io->iter, &io->iter_state);
			io->bytes_done += ret2;

			if (kiocb->ki_flags & IOCB_WRITE)
				io_req_end_write(req);
			return -EAGAIN;
		}
done:
		return kiocb_done(req, ret2, NULL, issue_flags);
	} else {
ret_eagain:
		iov_iter_restore(&io->iter, &io->iter_state);
		io_meta_restore(io, kiocb);
		if (kiocb->ki_flags & IOCB_WRITE)
			io_req_end_write(req);
		return -EAGAIN;
	}
}

int io_read_fixed(struct io_kiocb *req, unsigned int issue_flags)
{
	int ret;

	ret = io_init_rw_fixed(req, issue_flags, ITER_DEST);
	if (unlikely(ret))
		return ret;

	return io_read(req, issue_flags);
}

int io_write_fixed(struct io_kiocb *req, unsigned int issue_flags)
{
	int ret;

	ret = io_init_rw_fixed(req, issue_flags, ITER_SOURCE);
	if (unlikely(ret))
		return ret;

	return io_write(req, issue_flags);
}

void io_rw_fail(struct io_kiocb *req)
{
	int res;

	res = io_fixup_rw_res(req, req->cqe.res);
	io_req_set_res(req, res, req->cqe.flags);
}

static int io_uring_classic_poll(struct io_kiocb *req, struct io_comp_batch *iob,
				unsigned int poll_flags)
{
	struct file *file = req->file;

	if (req->opcode == IORING_OP_URING_CMD) {
		struct io_uring_cmd *ioucmd;

		ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
		return file->f_op->uring_cmd_iopoll(ioucmd, iob, poll_flags);
	} else {
		struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

		return file->f_op->iopoll(&rw->kiocb, iob, poll_flags);
	}
}

static u64 io_hybrid_iopoll_delay(struct io_ring_ctx *ctx, struct io_kiocb *req)
{
	struct hrtimer_sleeper timer;
	enum hrtimer_mode mode;
	ktime_t kt;
	u64 sleep_time;

	if (req->flags & REQ_F_IOPOLL_STATE)
		return 0;

	if (ctx->hybrid_poll_time == LLONG_MAX)
		return 0;

	/* Using half the running time to do schedule */
	sleep_time = ctx->hybrid_poll_time / 2;

	kt = ktime_set(0, sleep_time);
	req->flags |= REQ_F_IOPOLL_STATE;

	mode = HRTIMER_MODE_REL;
	hrtimer_setup_sleeper_on_stack(&timer, CLOCK_MONOTONIC, mode);
	hrtimer_set_expires(&timer.timer, kt);
	set_current_state(TASK_INTERRUPTIBLE);
	hrtimer_sleeper_start_expires(&timer, mode);

	if (timer.task)
		io_schedule();

	hrtimer_cancel(&timer.timer);
	__set_current_state(TASK_RUNNING);
	destroy_hrtimer_on_stack(&timer.timer);
	return sleep_time;
}

static int io_uring_hybrid_poll(struct io_kiocb *req,
				struct io_comp_batch *iob, unsigned int poll_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	u64 runtime, sleep_time;
	int ret;

	sleep_time = io_hybrid_iopoll_delay(ctx, req);
	ret = io_uring_classic_poll(req, iob, poll_flags);
	runtime = ktime_get_ns() - req->iopoll_start - sleep_time;

	/*
	 * Use minimum sleep time if we're polling devices with different
	 * latencies. We could get more completions from the faster ones.
	 */
	if (ctx->hybrid_poll_time > runtime)
		ctx->hybrid_poll_time = runtime;

	return ret;
}

int io_do_iopoll(struct io_ring_ctx *ctx, bool force_nonspin)
{
	struct io_wq_work_node *pos, *start, *prev;
	unsigned int poll_flags = 0;
	DEFINE_IO_COMP_BATCH(iob);
	int nr_events = 0;

	/*
	 * Only spin for completions if we don't have multiple devices hanging
	 * off our complete list.
	 */
	if (ctx->poll_multi_queue || force_nonspin)
		poll_flags |= BLK_POLL_ONESHOT;

	wq_list_for_each(pos, start, &ctx->iopoll_list) {
		struct io_kiocb *req = container_of(pos, struct io_kiocb, comp_list);
		int ret;

		/*
		 * Move completed and retryable entries to our local lists.
		 * If we find a request that requires polling, break out
		 * and complete those lists first, if we have entries there.
		 */
		if (READ_ONCE(req->iopoll_completed))
			break;

		if (ctx->flags & IORING_SETUP_HYBRID_IOPOLL)
			ret = io_uring_hybrid_poll(req, &iob, poll_flags);
		else
			ret = io_uring_classic_poll(req, &iob, poll_flags);

		if (unlikely(ret < 0))
			return ret;
		else if (ret)
			poll_flags |= BLK_POLL_ONESHOT;

		/* iopoll may have completed current req */
		if (!rq_list_empty(&iob.req_list) ||
		    READ_ONCE(req->iopoll_completed))
			break;
	}

	if (!rq_list_empty(&iob.req_list))
		iob.complete(&iob);
	else if (!pos)
		return 0;

	prev = start;
	wq_list_for_each_resume(pos, prev) {
		struct io_kiocb *req = container_of(pos, struct io_kiocb, comp_list);

		/* order with io_complete_rw_iopoll(), e.g. ->result updates */
		if (!smp_load_acquire(&req->iopoll_completed))
			break;
		nr_events++;
		req->cqe.flags = io_put_kbuf(req, req->cqe.res, NULL);
		if (req->opcode != IORING_OP_URING_CMD)
			io_req_rw_cleanup(req, 0);
	}
	if (unlikely(!nr_events))
		return 0;

	pos = start ? start->next : ctx->iopoll_list.first;
	wq_list_cut(&ctx->iopoll_list, prev, start);

	if (WARN_ON_ONCE(!wq_list_empty(&ctx->submit_state.compl_reqs)))
		return 0;
	ctx->submit_state.compl_reqs.first = pos;
	__io_submit_flush_completions(ctx);
	return nr_events;
}

void io_rw_cache_free(const void *entry)
{
	struct io_async_rw *rw = (struct io_async_rw *) entry;

	io_vec_free(&rw->vec);
	kfree(rw);
}
