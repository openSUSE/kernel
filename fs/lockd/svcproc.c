// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/lockd/svcproc.c
 *
 * Lockd server procedures. We don't implement the NLM_*_RES 
 * procedures because we don't use the async procedures.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/sunrpc/svc_xprt.h>

#include "lockd.h"

/*
 * xdr.h defines SM_PRIV_SIZE as a macro. nlm3xdr_gen.h defines
 * it as an enum constant. Undefine the macro before including
 * the generated header.
 */
#undef SM_PRIV_SIZE

#include "share.h"
#include "nlm3xdr_gen.h"

#define NLMDBG_FACILITY		NLMDBG_CLIENT

/*
 * Size of an NFSv2 file handle, in bytes, which is 32.
 * Defined locally to avoid including uapi/linux/nfs2.h.
 */
#define NLM3_FHSIZE		32

/*
 * Wrapper structures combine xdrgen types with legacy lockd_lock.
 * The xdrgen field must be first so the structure can be cast
 * to its XDR type for the RPC dispatch layer.
 */
struct nlm_testargs_wrapper {
	struct nlm_testargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm_testargs_wrapper, xdrgen) == 0);

struct nlm_testres_wrapper {
	struct nlm_testres		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm_testres_wrapper, xdrgen) == 0);

struct nlm_lockargs_wrapper {
	struct nlm_lockargs		xdrgen;
	struct lockd_cookie		cookie;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm_lockargs_wrapper, xdrgen) == 0);

struct nlm_res_wrapper {
	struct nlm_res			xdrgen;
};

static_assert(offsetof(struct nlm_res_wrapper, xdrgen) == 0);

struct nlm_cancargs_wrapper {
	struct nlm_cancargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm_cancargs_wrapper, xdrgen) == 0);

struct nlm_unlockargs_wrapper {
	struct nlm_unlockargs		xdrgen;
	struct lockd_lock		lock;
};

static_assert(offsetof(struct nlm_unlockargs_wrapper, xdrgen) == 0);

static __be32
nlm_netobj_to_cookie(struct lockd_cookie *cookie, netobj *object)
{
	if (object->len > NLM_MAXCOOKIELEN)
		return nlm_lck_denied_nolocks;
	cookie->len = object->len;
	memcpy(cookie->data, object->data, object->len);
	return nlm_granted;
}

static __be32
nlm_lock_to_lockd_lock(struct lockd_lock *lock, struct nlm_lock *alock)
{
	if (alock->fh.len != NLM3_FHSIZE)
		return nlm_lck_denied;
	lock->fh.size = alock->fh.len;
	memcpy(lock->fh.data, alock->fh.data, alock->fh.len);
	lock->oh.len = alock->oh.len;
	lock->oh.data = alock->oh.data;
	lock->svid = alock->uppid;
	lockd_set_file_lock_range3(&lock->fl, alock->l_offset, alock->l_len);
	return nlm_granted;
}

static struct nlm_host *
nlm3svc_lookup_host(struct svc_rqst *rqstp, string caller, bool monitored)
{
	struct nlm_host *host;

	if (!nlmsvc_ops)
		return NULL;
	host = nlmsvc_lookup_host(rqstp, caller.data, caller.len);
	if (!host)
		return NULL;
	if (monitored && nsm_monitor(host) < 0) {
		nlmsvc_release_host(host);
		return NULL;
	}
	return host;
}

static __be32
nlm3svc_lookup_file(struct svc_rqst *rqstp, struct nlm_host *host,
		    struct lockd_lock *lock, struct nlm_file **filp,
		    struct nlm_lock *xdr_lock, unsigned char type)
{
	bool is_test = (rqstp->rq_proc == NLMPROC_TEST ||
			rqstp->rq_proc == NLMPROC_TEST_MSG);
	struct file_lock *fl = &lock->fl;
	struct nlm_file *file = NULL;
	__be32 error;
	int mode;

	if (xdr_lock->fh.len != NLM3_FHSIZE)
		return nlm_lck_denied_nolocks;
	lock->fh.size = xdr_lock->fh.len;
	memcpy(lock->fh.data, xdr_lock->fh.data, xdr_lock->fh.len);

	lock->oh.len = xdr_lock->oh.len;
	lock->oh.data = xdr_lock->oh.data;

	lock->svid = xdr_lock->uppid;
	lock->lock_start = xdr_lock->l_offset;
	lock->lock_len = xdr_lock->l_len;

	locks_init_lock(fl);
	fl->c.flc_type = type;
	lockd_set_file_lock_range3(fl, lock->lock_start, lock->lock_len);

	mode = lock_to_openmode(fl);
	if (is_test)
		mode = O_RDWR;

	error = nlm_lookup_file(rqstp, &file, lock, mode);
	switch (error) {
	case nlm_granted:
		break;
	case nlm__int__stale_fh:
	case nlm__int__failed:
		return nlm_lck_denied_nolocks;
	default:
		return error;
	}
	*filp = file;

	fl->c.flc_flags = FL_POSIX;
	if (is_test)
		fl->c.flc_file = nlmsvc_file_file(file);
	else
		fl->c.flc_file = file->f_file[mode];
	fl->c.flc_pid = current->tgid;
	fl->fl_lmops = &nlmsvc_lock_operations;
	nlmsvc_locks_init_private(fl, host, (pid_t)lock->svid);
	if (!fl->c.flc_owner)
		return nlm_lck_denied_nolocks;

	return nlm_granted;
}

#ifdef CONFIG_LOCKD_V4
static inline __be32 cast_status(__be32 status)
{
	switch (status) {
	case nlm_granted:
	case nlm_lck_denied:
	case nlm_lck_denied_nolocks:
	case nlm_lck_blocked:
	case nlm_lck_denied_grace_period:
	case nlm__int__drop_reply:
		break;
	case nlm__int__deadlock:
		status = nlm_lck_denied;
		break;
	default:
		status = nlm_lck_denied_nolocks;
	}

	return status;
}
#else
static inline __be32 cast_status(__be32 status)
{
	switch (status) {
	case nlm__int__deadlock:
		status = nlm_lck_denied;
		break;
	case nlm__int__stale_fh:
	case nlm__int__failed:
		status = nlm_lck_denied_nolocks;
		break;
	default:
		if (be32_to_cpu(status) > be32_to_cpu(nlm__int__drop_reply))
			pr_warn_once("lockd: unhandled internal status %u\n",
				     be32_to_cpu(status));
		break;
	}
	return status;
}
#endif

/*
 * Obtain client and file from arguments
 */
static __be32
nlmsvc_retrieve_args(struct svc_rqst *rqstp, struct lockd_args *argp,
			struct nlm_host **hostp, struct nlm_file **filp)
{
	struct nlm_host		*host = NULL;
	struct nlm_file		*file = NULL;
	struct lockd_lock	*lock = &argp->lock;
	bool			is_test = (rqstp->rq_proc == NLMPROC_TEST ||
					   rqstp->rq_proc == NLMPROC_TEST_MSG);
	int			mode;
	__be32			error = 0;

	/* nfsd callbacks must have been installed for this procedure */
	if (!nlmsvc_ops)
		return nlm_lck_denied_nolocks;

	/* Obtain host handle */
	if (!(host = nlmsvc_lookup_host(rqstp, lock->caller, lock->len))
	 || (argp->monitor && nsm_monitor(host) < 0))
		goto no_locks;
	*hostp = host;

	/* Obtain file pointer. Not used by FREE_ALL call. */
	if (filp != NULL) {
		mode = lock_to_openmode(&lock->fl);

		if (is_test)
			mode = O_RDWR;

		error = cast_status(nlm_lookup_file(rqstp, &file, lock, mode));
		if (error != 0)
			goto no_locks;
		*filp = file;

		/* Set up the missing parts of the file_lock structure */
		lock->fl.c.flc_flags = FL_POSIX;
		if (is_test)
			lock->fl.c.flc_file = nlmsvc_file_file(file);
		else
			lock->fl.c.flc_file = file->f_file[mode];
		lock->fl.c.flc_pid = current->tgid;
		lock->fl.fl_lmops = &nlmsvc_lock_operations;
		nlmsvc_locks_init_private(&lock->fl, host, (pid_t)lock->svid);
		if (!lock->fl.c.flc_owner) {
			/* lockowner allocation has failed */
			nlmsvc_release_host(host);
			return nlm_lck_denied_nolocks;
		}
	}

	return 0;

no_locks:
	nlmsvc_release_host(host);
	if (error)
		return error;
	return nlm_lck_denied_nolocks;
}

/**
 * nlmsvc_proc_null - NULL: Test for presence of service
 * @rqstp: RPC transaction context
 *
 * Return:
 *   %rpc_success:		RPC executed successfully
 *
 * RPC synopsis:
 *   void NLM_NULL(void) = 0;
 */
static __be32 nlmsvc_proc_null(struct svc_rqst *rqstp)
{
	return rpc_success;
}

/**
 * nlmsvc_proc_test - TEST: Check for conflicting lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm_testres NLM_TEST(nlm_testargs) = 1;
 *
 * Permissible procedure status codes:
 *   %LCK_GRANTED:		The server would be able to grant the
 *				requested lock.
 *   %LCK_DENIED:		The requested lock conflicted with existing
 *				lock reservations for the file.
 *   %LCK_DENIED_NOLOCKS:	The server could not allocate the resources
 *				needed to process the request.
 *   %LCK_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 */
static __be32 nlmsvc_proc_test(struct svc_rqst *rqstp)
{
	struct nlm_testargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_testres_wrapper *resp = rqstp->rq_resp;
	struct nlm_file *file = NULL;
	struct nlm_host	*host;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.test_stat.stat = nlm_lck_denied_nolocks;
	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->xdrgen.test_stat.stat =
		nlm3svc_lookup_file(rqstp, host, &argp->lock, &file,
				    &argp->xdrgen.alock, type);
	if (resp->xdrgen.test_stat.stat)
		goto out;

	resp->xdrgen.test_stat.stat =
		cast_status(nlmsvc_testlock(rqstp, file, host, &argp->lock,
					    &resp->lock));
	nlmsvc_release_lockowner(&argp->lock);

	if (resp->xdrgen.test_stat.stat == nlm_lck_denied) {
		struct lockd_lock *conf = &resp->lock;
		struct nlm_holder *holder = &resp->xdrgen.test_stat.u.holder;

		holder->exclusive = (conf->fl.c.flc_type != F_RDLCK);
		holder->uppid = conf->svid;
		holder->oh.len = conf->oh.len;
		holder->oh.data = conf->oh.data;
		holder->l_offset = min_t(loff_t, conf->fl.fl_start,
					 NLM_OFFSET_MAX);
		if (conf->fl.fl_end == OFFSET_MAX)
			holder->l_len = 0;
		else
			holder->l_len = min_t(loff_t,
					      conf->fl.fl_end -
					      conf->fl.fl_start + 1,
					      NLM_OFFSET_MAX);
	}

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.test_stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

static __be32
__nlmsvc_proc_lock(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct lockd_args *argp = rqstp->rq_argp;
	struct nlm_host	*host;
	struct nlm_file	*file;
	__be32 rc = rpc_success;

	dprintk("lockd: LOCK          called\n");

	resp->cookie = argp->cookie;

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm__int__drop_reply ?
			rpc_drop_reply : rpc_success;

	/* Now try to lock the file */
	resp->status = cast_status(nlmsvc_lock(rqstp, file, host, &argp->lock,
					       argp->block, &argp->cookie,
					       argp->reclaim));
	if (resp->status == nlm__int__drop_reply)
		rc = rpc_drop_reply;
	else
		dprintk("lockd: LOCK         status %d\n", ntohl(resp->status));

	nlmsvc_release_lockowner(&argp->lock);
	nlmsvc_release_host(host);
	nlm_release_file(file);
	return rc;
}

static __be32
nlmsvc_do_lock(struct svc_rqst *rqstp, bool monitored)
{
	struct nlm_lockargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_res_wrapper *resp = rqstp->rq_resp;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_netobj_to_cookie(&argp->cookie,
						      &argp->xdrgen.cookie);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name,
				   monitored);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm3svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     type);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = cast_status(nlmsvc_lock(rqstp, file, host,
							 &argp->lock,
							 argp->xdrgen.block,
							 &argp->cookie,
							 argp->xdrgen.reclaim));

	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlmsvc_proc_lock - LOCK: Establish a monitored lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm_res NLM_LOCK(nlm_lockargs) = 2;
 *
 * Permissible procedure status codes:
 *   %LCK_GRANTED:		The requested lock was granted.
 *   %LCK_DENIED:		The requested lock conflicted with existing
 *				lock reservations for the file.
 *   %LCK_DENIED_NOLOCKS:	The server could not allocate the resources
 *				needed to process the request.
 *   %LCK_BLOCKED:		The blocking request cannot be granted
 *				immediately. The server will send an
 *				NLM_GRANTED callback to the client when
 *				the lock can be granted.
 *   %LCK_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 */
static __be32
nlmsvc_proc_lock(struct svc_rqst *rqstp)
{
	return nlmsvc_do_lock(rqstp, true);
}

/**
 * nlmsvc_proc_cancel - CANCEL: Cancel an outstanding blocked lock request
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm_res NLM_CANCEL(nlm_cancargs) = 3;
 *
 * Permissible procedure status codes:
 *   %LCK_GRANTED:		The requested lock was canceled.
 *   %LCK_DENIED:		There was no lock to cancel.
 *   %LCK_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %LCK_DENIED_NOLOCKS:	A needed resource could not be allocated.
 */
static __be32
nlmsvc_proc_cancel(struct svc_rqst *rqstp)
{
	struct nlm_cancargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_res_wrapper *resp = rqstp->rq_resp;
	struct net *net = SVC_NET(rqstp);
	struct nlm_host	*host = NULL;
	struct nlm_file	*file = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm3svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     type);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmsvc_cancel_blocked(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/**
 * nlmsvc_proc_unlock - UNLOCK: Remove a lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *
 * RPC synopsis:
 *   nlm_res NLM_UNLOCK(nlm_unlockargs) = 4;
 *
 * Permissible procedure status codes:
 *   %LCK_GRANTED:		The requested lock was released.
 *   %LCK_DENIED_GRACE_PERIOD:	The server has recently restarted and is
 *				re-establishing existing locks, and is not
 *				yet ready to accept normal service requests.
 *
 * The Linux NLM server implementation also returns:
 *   %LCK_DENIED_NOLOCKS:	A needed resource could not be allocated.
 */
static __be32
nlmsvc_proc_unlock(struct svc_rqst *rqstp)
{
	struct nlm_unlockargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_res_wrapper *resp = rqstp->rq_resp;
	struct net *net = SVC_NET(rqstp);
	struct nlm_host	*host = NULL;
	struct nlm_file	*file = NULL;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->xdrgen.stat.stat = nlm_lck_denied_nolocks;
	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->xdrgen.stat.stat = nlm3svc_lookup_file(rqstp, host, &argp->lock,
						     &file, &argp->xdrgen.alock,
						     F_UNLCK);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmsvc_unlock(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->xdrgen.stat.stat == nlm__int__drop_reply ?
		rpc_drop_reply : rpc_success;
}

/*
 * GRANTED: A server calls us to tell that a process' lock request
 * was granted
 */
static __be32
__nlmsvc_proc_granted(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct lockd_args *argp = rqstp->rq_argp;

	resp->cookie = argp->cookie;

	dprintk("lockd: GRANTED       called\n");
	resp->status = nlmclnt_grant(svc_addr(rqstp), &argp->lock);
	dprintk("lockd: GRANTED       status %d\n", ntohl(resp->status));
	return rpc_success;
}

/**
 * nlmsvc_proc_granted - GRANTED: Blocked lock has been granted
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *
 * RPC synopsis:
 *   nlm_res NLM_GRANTED(nlm_testargs) = 5;
 *
 * Permissible procedure status codes:
 *   %LCK_GRANTED:		The granted lock was accepted.
 *   %LCK_DENIED:		The procedure failed, possibly due to
 *				internal resource constraints.
 *   %LCK_DENIED_GRACE_PERIOD:	The client host recently restarted and
 *				its NLM is re-establishing existing locks,
 *				so it is not yet ready to accept callbacks.
 */
static __be32
nlmsvc_proc_granted(struct svc_rqst *rqstp)
{
	struct nlm_testargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_res_wrapper *resp = rqstp->rq_resp;

	resp->xdrgen.cookie = argp->xdrgen.cookie;

	resp->xdrgen.stat.stat = nlm_lock_to_lockd_lock(&argp->lock,
							&argp->xdrgen.alock);
	if (resp->xdrgen.stat.stat)
		goto out;

	resp->xdrgen.stat.stat = nlmclnt_grant(svc_addr(rqstp), &argp->lock);

out:
	return rpc_success;
}

/*
 * This is the generic lockd callback for async RPC calls
 */
static void nlmsvc_callback_exit(struct rpc_task *task, void *data)
{
}

void nlmsvc_release_call(struct nlm_rqst *call)
{
	if (!refcount_dec_and_test(&call->a_count))
		return;
	nlmsvc_release_host(call->a_host);
	kfree(call);
}

static void nlmsvc_callback_release(void *data)
{
	nlmsvc_release_call(data);
}

static const struct rpc_call_ops nlmsvc_callback_ops = {
	.rpc_call_done = nlmsvc_callback_exit,
	.rpc_release = nlmsvc_callback_release,
};

/*
 * `Async' versions of the above service routines. They aren't really,
 * because we send the callback before the reply proper. I hope this
 * doesn't break any clients.
 */
static __be32
nlmsvc_callback(struct svc_rqst *rqstp, struct nlm_host *host, u32 proc,
		__be32 (*func)(struct svc_rqst *, struct lockd_res *))
{
	struct nlm_rqst	*call;
	__be32 stat;

	call = nlm_alloc_call(host);
	nlmsvc_release_host(host);
	if (call == NULL)
		return rpc_system_err;

	stat = func(rqstp, &call->a_res);
	if (stat != 0) {
		nlmsvc_release_call(call);
		return stat;
	}

	call->a_flags = RPC_TASK_ASYNC;
	if (nlm_async_reply(call, proc, &nlmsvc_callback_ops) < 0)
		return rpc_system_err;
	return rpc_success;
}

static __be32
__nlmsvc_proc_test_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm_testargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_lockowner *owner;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->status = nlm3svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, type);
	if (resp->status)
		goto out;

	owner = argp->lock.fl.c.flc_owner;
	resp->status = cast_status(nlmsvc_testlock(rqstp, file, host,
						   &argp->lock, &resp->lock));
	nlmsvc_put_lockowner(owner);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ? rpc_drop_reply : rpc_success;
}

/**
 * nlmsvc_proc_test_msg - TEST_MSG: Check for conflicting lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *   %rpc_garbage_args:	The request arguments are malformed.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC_TEST_MSG(nlm_testargs) = 6;
 *
 * The response to this request is delivered via the TEST_RES procedure.
 */
static __be32 nlmsvc_proc_test_msg(struct svc_rqst *rqstp)
{
	struct nlm_testargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	if (argp->xdrgen.cookie.len > NLM_MAXCOOKIELEN)
		return rpc_garbage_args;

	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlmsvc_callback(rqstp, host, NLMPROC_TEST_RES,
			       __nlmsvc_proc_test_msg);
}

static __be32
__nlmsvc_proc_lock_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm_lockargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, true);
	if (!host)
		goto out;

	resp->status = nlm3svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, type);
	if (resp->status)
		goto out;

	resp->status = cast_status(nlmsvc_lock(rqstp, file, host, &argp->lock,
					       argp->xdrgen.block, &resp->cookie,
					       argp->xdrgen.reclaim));
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ? rpc_drop_reply : rpc_success;
}

/**
 * nlmsvc_proc_lock_msg - LOCK_MSG: Establish a monitored lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *   %rpc_garbage_args:	The request arguments are malformed.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC_LOCK_MSG(nlm_lockargs) = 7;
 *
 * The response to this request is delivered via the LOCK_RES procedure.
 */
static __be32 nlmsvc_proc_lock_msg(struct svc_rqst *rqstp)
{
	struct nlm_lockargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	if (argp->xdrgen.cookie.len > NLM_MAXCOOKIELEN)
		return rpc_garbage_args;

	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlmsvc_callback(rqstp, host, NLMPROC_LOCK_RES,
			       __nlmsvc_proc_lock_msg);
}

static __be32
__nlmsvc_proc_cancel_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm_cancargs_wrapper *argp = rqstp->rq_argp;
	unsigned char type = argp->xdrgen.exclusive ? F_WRLCK : F_RDLCK;
	struct net *net = SVC_NET(rqstp);
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	resp->status = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->status = nlm_lck_denied_nolocks;
	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->status = nlm3svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, type);
	if (resp->status)
		goto out;

	resp->status = nlmsvc_cancel_blocked(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ? rpc_drop_reply : rpc_success;
}

/**
 * nlmsvc_proc_cancel_msg - CANCEL_MSG: Cancel an outstanding lock request
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *   %rpc_garbage_args:	The request arguments are malformed.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC_CANCEL_MSG(nlm_cancargs) = 8;
 *
 * The response to this request is delivered via the CANCEL_RES procedure.
 */
static __be32 nlmsvc_proc_cancel_msg(struct svc_rqst *rqstp)
{
	struct nlm_cancargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	if (argp->xdrgen.cookie.len > NLM_MAXCOOKIELEN)
		return rpc_garbage_args;

	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlmsvc_callback(rqstp, host, NLMPROC_CANCEL_RES,
			       __nlmsvc_proc_cancel_msg);
}

static __be32
__nlmsvc_proc_unlock_msg(struct svc_rqst *rqstp, struct lockd_res *resp)
{
	struct nlm_unlockargs_wrapper *argp = rqstp->rq_argp;
	struct net *net = SVC_NET(rqstp);
	struct nlm_file	*file = NULL;
	struct nlm_host	*host = NULL;

	resp->status = nlm_lck_denied_nolocks;
	if (nlm_netobj_to_cookie(&resp->cookie, &argp->xdrgen.cookie))
		goto out;

	resp->status = nlm_lck_denied_grace_period;
	if (locks_in_grace(net))
		goto out;

	resp->status = nlm_lck_denied_nolocks;
	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		goto out;

	resp->status = nlm3svc_lookup_file(rqstp, host, &argp->lock,
					   &file, &argp->xdrgen.alock, F_UNLCK);
	if (resp->status)
		goto out;

	resp->status = nlmsvc_unlock(net, file, &argp->lock);
	nlmsvc_release_lockowner(&argp->lock);

out:
	if (file)
		nlm_release_file(file);
	nlmsvc_release_host(host);
	return resp->status == nlm__int__drop_reply ? rpc_drop_reply : rpc_success;
}

/**
 * nlmsvc_proc_unlock_msg - UNLOCK_MSG: Remove an existing lock
 * @rqstp: RPC transaction context
 *
 * Returns:
 *   %rpc_success:		RPC executed successfully.
 *   %rpc_drop_reply:		Do not send an RPC reply.
 *   %rpc_garbage_args:	The request arguments are malformed.
 *   %rpc_system_err:		RPC execution failed.
 *
 * RPC synopsis:
 *   void NLMPROC_UNLOCK_MSG(nlm_unlockargs) = 9;
 *
 * The response to this request is delivered via the UNLOCK_RES procedure.
 */
static __be32 nlmsvc_proc_unlock_msg(struct svc_rqst *rqstp)
{
	struct nlm_unlockargs_wrapper *argp = rqstp->rq_argp;
	struct nlm_host *host;

	if (argp->xdrgen.cookie.len > NLM_MAXCOOKIELEN)
		return rpc_garbage_args;

	host = nlm3svc_lookup_host(rqstp, argp->xdrgen.alock.caller_name, false);
	if (!host)
		return rpc_system_err;

	return nlmsvc_callback(rqstp, host, NLMPROC_UNLOCK_RES,
			       __nlmsvc_proc_unlock_msg);
}

static __be32 nlmsvc_proc_granted_msg(struct svc_rqst *rqstp)
{
	struct lockd_args *argp = rqstp->rq_argp;
	struct nlm_host	*host;

	dprintk("lockd: GRANTED_MSG   called\n");

	host = nlmsvc_lookup_host(rqstp, argp->lock.caller, argp->lock.len);
	if (!host)
		return rpc_system_err;

	return nlmsvc_callback(rqstp, host, NLMPROC_GRANTED_RES,
			       __nlmsvc_proc_granted);
}

/*
 * SHARE: create a DOS share or alter existing share.
 */
static __be32
nlmsvc_proc_share(struct svc_rqst *rqstp)
{
	struct lockd_args *argp = rqstp->rq_argp;
	struct lockd_res *resp = rqstp->rq_resp;
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: SHARE         called\n");

	resp->cookie = argp->cookie;

	/* Don't accept new lock requests during grace period */
	if (locks_in_grace(SVC_NET(rqstp)) && !argp->reclaim) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm__int__drop_reply ?
			rpc_drop_reply : rpc_success;

	/* Now try to create the share */
	resp->status = cast_status(nlmsvc_share_file(host, file, &argp->lock.oh,
						     argp->fsm_access,
						     argp->fsm_mode));

	dprintk("lockd: SHARE         status %d\n", ntohl(resp->status));
	nlmsvc_release_lockowner(&argp->lock);
	nlmsvc_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * UNSHARE: Release a DOS share.
 */
static __be32
nlmsvc_proc_unshare(struct svc_rqst *rqstp)
{
	struct lockd_args *argp = rqstp->rq_argp;
	struct lockd_res *resp = rqstp->rq_resp;
	struct nlm_host	*host;
	struct nlm_file	*file;

	dprintk("lockd: UNSHARE       called\n");

	resp->cookie = argp->cookie;

	/* Don't accept requests during grace period */
	if (locks_in_grace(SVC_NET(rqstp))) {
		resp->status = nlm_lck_denied_grace_period;
		return rpc_success;
	}

	/* Obtain client and file */
	if ((resp->status = nlmsvc_retrieve_args(rqstp, argp, &host, &file)))
		return resp->status == nlm__int__drop_reply ?
			rpc_drop_reply : rpc_success;

	/* Now try to unshare the file */
	resp->status = cast_status(nlmsvc_unshare_file(host, file,
						       &argp->lock.oh));

	dprintk("lockd: UNSHARE       status %d\n", ntohl(resp->status));
	nlmsvc_release_lockowner(&argp->lock);
	nlmsvc_release_host(host);
	nlm_release_file(file);
	return rpc_success;
}

/*
 * NM_LOCK: Create an unmonitored lock
 */
static __be32
nlmsvc_proc_nm_lock(struct svc_rqst *rqstp)
{
	struct lockd_args *argp = rqstp->rq_argp;

	dprintk("lockd: NM_LOCK       called\n");

	argp->monitor = 0;		/* just clean the monitor flag */
	return __nlmsvc_proc_lock(rqstp, rqstp->rq_resp);
}

/*
 * FREE_ALL: Release all locks and shares held by client
 */
static __be32
nlmsvc_proc_free_all(struct svc_rqst *rqstp)
{
	struct lockd_args *argp = rqstp->rq_argp;
	struct nlm_host	*host;

	/* Obtain client */
	if (nlmsvc_retrieve_args(rqstp, argp, &host, NULL))
		return rpc_success;

	nlmsvc_free_host_resources(host);
	nlmsvc_release_host(host);
	return rpc_success;
}

/*
 * SM_NOTIFY: private callback from statd (not part of official NLM proto)
 */
static __be32
nlmsvc_proc_sm_notify(struct svc_rqst *rqstp)
{
	struct lockd_reboot *argp = rqstp->rq_argp;

	dprintk("lockd: SM_NOTIFY     called\n");

	if (!nlm_privileged_requester(rqstp)) {
		char buf[RPC_MAX_ADDRBUFLEN];
		printk(KERN_WARNING "lockd: rejected NSM callback from %s\n",
				svc_print_addr(rqstp, buf, sizeof(buf)));
		return rpc_system_err;
	}

	nlm_host_rebooted(SVC_NET(rqstp), argp);
	return rpc_success;
}

/*
 * client sent a GRANTED_RES, let's remove the associated block
 */
static __be32
nlmsvc_proc_granted_res(struct svc_rqst *rqstp)
{
	struct lockd_res *argp = rqstp->rq_argp;

	if (!nlmsvc_ops)
		return rpc_success;

	dprintk("lockd: GRANTED_RES   called\n");

	nlmsvc_grant_reply(&argp->cookie, argp->status);
	return rpc_success;
}

static __be32
nlmsvc_proc_unused(struct svc_rqst *rqstp)
{
	return rpc_proc_unavail;
}

/*
 * NLM Server procedures.
 */

struct nlm_void			{ int dummy; };

#define	Ck	(1+XDR_QUADLEN(NLM_MAXCOOKIELEN))	/* cookie */
#define	St	1				/* status */
#define	No	(1+1024/4)			/* Net Obj */
#define	Rg	2				/* range - offset + size */

static const struct svc_procedure nlmsvc_procedures[24] = {
	[NLM_NULL] = {
		.pc_func	= nlmsvc_proc_null,
		.pc_decode	= nlm_svc_decode_void,
		.pc_encode	= nlm_svc_encode_void,
		.pc_argsize	= XDR_void,
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "NULL",
	},
	[NLM_TEST] = {
		.pc_func	= nlmsvc_proc_test,
		.pc_decode	= nlm_svc_decode_nlm_testargs,
		.pc_encode	= nlm_svc_encode_nlm_testres,
		.pc_argsize	= sizeof(struct nlm_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm_testres_wrapper),
		.pc_xdrressize	= NLM3_nlm_testres_sz,
		.pc_name	= "TEST",
	},
	[NLM_LOCK] = {
		.pc_func	= nlmsvc_proc_lock,
		.pc_decode	= nlm_svc_decode_nlm_lockargs,
		.pc_encode	= nlm_svc_encode_nlm_res,
		.pc_argsize	= sizeof(struct nlm_lockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm_res_wrapper),
		.pc_xdrressize	= NLM3_nlm_res_sz,
		.pc_name	= "LOCK",
	},
	[NLM_CANCEL] = {
		.pc_func	= nlmsvc_proc_cancel,
		.pc_decode	= nlm_svc_decode_nlm_cancargs,
		.pc_encode	= nlm_svc_encode_nlm_res,
		.pc_argsize	= sizeof(struct nlm_cancargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm_res_wrapper),
		.pc_xdrressize	= NLM3_nlm_res_sz,
		.pc_name	= "CANCEL",
	},
	[NLM_UNLOCK] = {
		.pc_func	= nlmsvc_proc_unlock,
		.pc_decode	= nlm_svc_decode_nlm_unlockargs,
		.pc_encode	= nlm_svc_encode_nlm_res,
		.pc_argsize	= sizeof(struct nlm_unlockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm_res_wrapper),
		.pc_xdrressize	= NLM3_nlm_res_sz,
		.pc_name	= "UNLOCK",
	},
	[NLM_GRANTED] = {
		.pc_func	= nlmsvc_proc_granted,
		.pc_decode	= nlm_svc_decode_nlm_testargs,
		.pc_encode	= nlm_svc_encode_nlm_res,
		.pc_argsize	= sizeof(struct nlm_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= sizeof(struct nlm_res_wrapper),
		.pc_xdrressize	= NLM3_nlm_res_sz,
		.pc_name	= "GRANTED",
	},
	[NLM_TEST_MSG] = {
		.pc_func	= nlmsvc_proc_test_msg,
		.pc_decode	= nlm_svc_decode_nlm_testargs,
		.pc_encode	= nlm_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm_testargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "TEST_MSG",
	},
	[NLM_LOCK_MSG] = {
		.pc_func	= nlmsvc_proc_lock_msg,
		.pc_decode	= nlm_svc_decode_nlm_lockargs,
		.pc_encode	= nlm_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm_lockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "LOCK_MSG",
	},
	[NLM_CANCEL_MSG] = {
		.pc_func	= nlmsvc_proc_cancel_msg,
		.pc_decode	= nlm_svc_decode_nlm_cancargs,
		.pc_encode	= nlm_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm_cancargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "CANCEL_MSG",
	},
	[NLM_UNLOCK_MSG] = {
		.pc_func	= nlmsvc_proc_unlock_msg,
		.pc_decode	= nlm_svc_decode_nlm_unlockargs,
		.pc_encode	= nlm_svc_encode_void,
		.pc_argsize	= sizeof(struct nlm_unlockargs_wrapper),
		.pc_argzero	= 0,
		.pc_ressize	= 0,
		.pc_xdrressize	= XDR_void,
		.pc_name	= "UNLOCK_MSG",
	},
	[NLMPROC_GRANTED_MSG] = {
		.pc_func = nlmsvc_proc_granted_msg,
		.pc_decode = nlmsvc_decode_testargs,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_args),
		.pc_argzero = sizeof(struct lockd_args),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "GRANTED_MSG",
	},
	[NLMPROC_TEST_RES] = {
		.pc_func = nlmsvc_proc_null,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_res),
		.pc_argzero = sizeof(struct lockd_res),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "TEST_RES",
	},
	[NLMPROC_LOCK_RES] = {
		.pc_func = nlmsvc_proc_null,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_res),
		.pc_argzero = sizeof(struct lockd_res),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "LOCK_RES",
	},
	[NLMPROC_CANCEL_RES] = {
		.pc_func = nlmsvc_proc_null,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_res),
		.pc_argzero = sizeof(struct lockd_res),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "CANCEL_RES",
	},
	[NLMPROC_UNLOCK_RES] = {
		.pc_func = nlmsvc_proc_null,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_res),
		.pc_argzero = sizeof(struct lockd_res),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "UNLOCK_RES",
	},
	[NLMPROC_GRANTED_RES] = {
		.pc_func = nlmsvc_proc_granted_res,
		.pc_decode = nlmsvc_decode_res,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_res),
		.pc_argzero = sizeof(struct lockd_res),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "GRANTED_RES",
	},
	[NLMPROC_NSM_NOTIFY] = {
		.pc_func = nlmsvc_proc_sm_notify,
		.pc_decode = nlmsvc_decode_reboot,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_reboot),
		.pc_argzero = sizeof(struct lockd_reboot),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "SM_NOTIFY",
	},
	[17] = {
		.pc_func = nlmsvc_proc_unused,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct nlm_void),
		.pc_argzero = sizeof(struct nlm_void),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "UNUSED",
	},
	[18] = {
		.pc_func = nlmsvc_proc_unused,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct nlm_void),
		.pc_argzero = sizeof(struct nlm_void),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "UNUSED",
	},
	[19] = {
		.pc_func = nlmsvc_proc_unused,
		.pc_decode = nlmsvc_decode_void,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct nlm_void),
		.pc_argzero = sizeof(struct nlm_void),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = St,
		.pc_name = "UNUSED",
	},
	[NLMPROC_SHARE] = {
		.pc_func = nlmsvc_proc_share,
		.pc_decode = nlmsvc_decode_shareargs,
		.pc_encode = nlmsvc_encode_shareres,
		.pc_argsize = sizeof(struct lockd_args),
		.pc_argzero = sizeof(struct lockd_args),
		.pc_ressize = sizeof(struct lockd_res),
		.pc_xdrressize = Ck+St+1,
		.pc_name = "SHARE",
	},
	[NLMPROC_UNSHARE] = {
		.pc_func = nlmsvc_proc_unshare,
		.pc_decode = nlmsvc_decode_shareargs,
		.pc_encode = nlmsvc_encode_shareres,
		.pc_argsize = sizeof(struct lockd_args),
		.pc_argzero = sizeof(struct lockd_args),
		.pc_ressize = sizeof(struct lockd_res),
		.pc_xdrressize = Ck+St+1,
		.pc_name = "UNSHARE",
	},
	[NLMPROC_NM_LOCK] = {
		.pc_func = nlmsvc_proc_nm_lock,
		.pc_decode = nlmsvc_decode_lockargs,
		.pc_encode = nlmsvc_encode_res,
		.pc_argsize = sizeof(struct lockd_args),
		.pc_argzero = sizeof(struct lockd_args),
		.pc_ressize = sizeof(struct lockd_res),
		.pc_xdrressize = Ck+St,
		.pc_name = "NM_LOCK",
	},
	[NLMPROC_FREE_ALL] = {
		.pc_func = nlmsvc_proc_free_all,
		.pc_decode = nlmsvc_decode_notify,
		.pc_encode = nlmsvc_encode_void,
		.pc_argsize = sizeof(struct lockd_args),
		.pc_argzero = sizeof(struct lockd_args),
		.pc_ressize = sizeof(struct nlm_void),
		.pc_xdrressize = 0,
		.pc_name = "FREE_ALL",
	},
};

/*
 * Storage requirements for XDR arguments and results
 */
union nlmsvc_xdrstore {
	struct nlm_testargs_wrapper	testargs;
	struct nlm_lockargs_wrapper	lockargs;
	struct nlm_cancargs_wrapper	cancargs;
	struct nlm_unlockargs_wrapper	unlockargs;
	struct nlm_testres_wrapper	testres;
	struct nlm_res_wrapper		res;
	struct lockd_args		args;
	struct lockd_reboot		reboot;
};

/*
 * NLMv1 defines only procedures 1 - 15. Linux lockd also implements
 * procedures 0 (NULL) and 16 (SM_NOTIFY).
 */
static DEFINE_PER_CPU_ALIGNED(unsigned long, nlm1svc_call_counters[17]);

const struct svc_version nlmsvc_version1 = {
	.vs_vers	= 1,
	.vs_nproc	= 17,
	.vs_proc	= nlmsvc_procedures,
	.vs_count	= nlm1svc_call_counters,
	.vs_dispatch	= nlmsvc_dispatch,
	.vs_xdrsize	= sizeof(union nlmsvc_xdrstore),
};

static DEFINE_PER_CPU_ALIGNED(unsigned long,
			      nlm3svc_call_counters[ARRAY_SIZE(nlmsvc_procedures)]);

const struct svc_version nlmsvc_version3 = {
	.vs_vers	= 3,
	.vs_nproc	= ARRAY_SIZE(nlmsvc_procedures),
	.vs_proc	= nlmsvc_procedures,
	.vs_count	= nlm3svc_call_counters,
	.vs_dispatch	= nlmsvc_dispatch,
	.vs_xdrsize	= sizeof(union nlmsvc_xdrstore),
};
