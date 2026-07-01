/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

/* Bluetooth address family and sockets. */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <net/sock.h>
#include <asm/ioctls.h>
#include <linux/kmod.h>

#include <net/bluetooth/bluetooth.h>

#define VERSION "2.16"

/* Bluetooth sockets */
#define BT_MAX_PROTO	8
static const struct net_proto_family *bt_proto[BT_MAX_PROTO];
static DEFINE_RWLOCK(bt_proto_lock);

static struct lock_class_key bt_lock_key[BT_MAX_PROTO];
static const char *const bt_key_strings[BT_MAX_PROTO] = {
	"sk_lock-AF_BLUETOOTH-BTPROTO_L2CAP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_HCI",
	"sk_lock-AF_BLUETOOTH-BTPROTO_SCO",
	"sk_lock-AF_BLUETOOTH-BTPROTO_RFCOMM",
	"sk_lock-AF_BLUETOOTH-BTPROTO_BNEP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_CMTP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_HIDP",
	"sk_lock-AF_BLUETOOTH-BTPROTO_AVDTP",
};

static struct lock_class_key bt_slock_key[BT_MAX_PROTO];
static const char *const bt_slock_key_strings[BT_MAX_PROTO] = {
	"slock-AF_BLUETOOTH-BTPROTO_L2CAP",
	"slock-AF_BLUETOOTH-BTPROTO_HCI",
	"slock-AF_BLUETOOTH-BTPROTO_SCO",
	"slock-AF_BLUETOOTH-BTPROTO_RFCOMM",
	"slock-AF_BLUETOOTH-BTPROTO_BNEP",
	"slock-AF_BLUETOOTH-BTPROTO_CMTP",
	"slock-AF_BLUETOOTH-BTPROTO_HIDP",
	"slock-AF_BLUETOOTH-BTPROTO_AVDTP",
};

static inline void bt_sock_reclassify_lock(struct socket *sock, int proto)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return;

	BUG_ON(sock_owned_by_user(sk));

	sock_lock_init_class_and_name(sk,
			bt_slock_key_strings[proto], &bt_slock_key[proto],
				bt_key_strings[proto], &bt_lock_key[proto]);
}

int bt_sock_register(int proto, const struct net_proto_family *ops)
{
	int err = 0;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	write_lock(&bt_proto_lock);

	if (bt_proto[proto])
		err = -EEXIST;
	else
		bt_proto[proto] = ops;

	write_unlock(&bt_proto_lock);

	return err;
}
EXPORT_SYMBOL(bt_sock_register);

int bt_sock_unregister(int proto)
{
	int err = 0;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	write_lock(&bt_proto_lock);

	if (!bt_proto[proto])
		err = -ENOENT;
	else
		bt_proto[proto] = NULL;

	write_unlock(&bt_proto_lock);

	return err;
}
EXPORT_SYMBOL(bt_sock_unregister);

static int bt_sock_create(struct net *net, struct socket *sock, int proto,
			  int kern)
{
	int err;

	if (net != &init_net)
		return -EAFNOSUPPORT;

	if (proto < 0 || proto >= BT_MAX_PROTO)
		return -EINVAL;

	if (!bt_proto[proto])
		request_module("bt-proto-%d", proto);

	err = -EPROTONOSUPPORT;

	read_lock(&bt_proto_lock);

	if (bt_proto[proto] && try_module_get(bt_proto[proto]->owner)) {
		err = bt_proto[proto]->create(net, sock, proto, kern);
		bt_sock_reclassify_lock(sock, proto);
		module_put(bt_proto[proto]->owner);
	}

	read_unlock(&bt_proto_lock);

	return err;
}

struct sock *bt_sock_alloc(struct net *net, struct socket *sock,
			   struct proto *prot, int proto, gfp_t prio)
{
	struct sock *sk;

	sk = sk_alloc(net, PF_BLUETOOTH, prio, prot);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);
	spin_lock_init(&bt_sk(sk)->accept_q_lock);

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state    = BT_OPEN;

	return sk;
}
EXPORT_SYMBOL(bt_sock_alloc);

void bt_sock_link(struct bt_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}
EXPORT_SYMBOL(bt_sock_link);

void bt_sock_unlink(struct bt_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}
EXPORT_SYMBOL(bt_sock_unlink);

void bt_accept_enqueue(struct sock *parent, struct sock *sk)
{
	struct bt_sock *par = bt_sk(parent);

	BT_DBG("parent %p, sk %p", parent, sk);

	sock_hold(sk);
	bt_sk(sk)->parent = parent;

	spin_lock_bh(&par->accept_q_lock);
	list_add_tail(&bt_sk(sk)->accept_q, &par->accept_q);
	parent->sk_ack_backlog++;
	spin_unlock_bh(&par->accept_q_lock);
}
EXPORT_SYMBOL(bt_accept_enqueue);

void bt_accept_unlink(struct sock *sk)
{
	struct sock *parent = bt_sk(sk)->parent;

	BT_DBG("sk %p state %d", sk, sk->sk_state);

	spin_lock_bh(&bt_sk(parent)->accept_q_lock);
	list_del_init(&bt_sk(sk)->accept_q);
	parent->sk_ack_backlog--;
	spin_unlock_bh(&bt_sk(parent)->accept_q_lock);
	bt_sk(sk)->parent = NULL;
	sock_put(sk);
}
EXPORT_SYMBOL(bt_accept_unlink);

static struct sock *bt_accept_get(struct sock *parent, struct sock *sk)
{
	struct bt_sock *bt = bt_sk(parent);
	struct sock *next = NULL;

	/* accept_q is modified from child teardown paths too, so take a
	 * temporary reference before dropping the queue lock.
	 */
	spin_lock_bh(&bt->accept_q_lock);

	if (sk) {
		if (bt_sk(sk)->parent != parent)
			goto out;

		if (!list_is_last(&bt_sk(sk)->accept_q, &bt->accept_q)) {
			next = &list_entry(bt_sk(sk)->accept_q.next, struct bt_sock, accept_q)->sk;
			sock_hold(next);
		}
	} else if (!list_empty(&bt->accept_q)) {
		next = &list_first_entry(&bt->accept_q,
					 struct bt_sock, accept_q)->sk;
		sock_hold(next);
	}

out:
	spin_unlock_bh(&bt->accept_q_lock);
	return next;
}

struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock)
{
	struct sock *sk, *next;

	BT_DBG("parent %p", parent);

restart:
	for (sk = bt_accept_get(parent, NULL); sk; sk = next) {
		/* Prevent early freeing of sk due to unlink and sock_kill */
		lock_sock(sk);

		/* Check sk has not already been unlinked via
		 * bt_accept_unlink() due to serialisation caused by sk locking
		 */
		if (bt_sk(sk)->parent != parent) {
			BT_DBG("sk %p, already unlinked", sk);
			release_sock(sk);
			sock_put(sk);

			goto restart;
		}

		next = bt_accept_get(parent, sk);

		/* sk is safely in the parent list so reduce reference count */
		sock_put(sk);

		/* FIXME: Is this check still needed */
		if (sk->sk_state == BT_CLOSED) {
			release_sock(sk);
			bt_accept_unlink(sk);
			if (next)
				sock_put(next);
			continue;
		}

		if (sk->sk_state == BT_CONNECTED || !newsock ||
						bt_sk(parent)->defer_setup) {
			bt_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);

			release_sock(sk);
			if (next)
				sock_put(next);
			return sk;
		}

		release_sock(sk);
	}

	return NULL;
}
EXPORT_SYMBOL(bt_accept_dequeue);

int bt_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
				struct msghdr *msg, size_t len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	size_t copied;
	int err;

	BT_DBG("sock %p sk %p len %zu", sock, sk, len);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	lock_sock(sk);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb) {
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			err = 0;

		release_sock(sk);
		return err;
	}

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	skb_reset_transport_header(skb);
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err == 0)
		sock_recv_ts_and_drops(msg, sk, skb);

	skb_free_datagram(sk, skb);

	release_sock(sk);

	return err ? : copied;
}
EXPORT_SYMBOL(bt_sock_recvmsg);

static long bt_sock_data_wait(struct sock *sk, long timeo)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(sk_sleep(sk), &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!skb_queue_empty(&sk->sk_receive_queue))
			break;

		if (sk->sk_err || (sk->sk_shutdown & RCV_SHUTDOWN))
			break;

		if (signal_pending(current) || !timeo)
			break;

		set_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		clear_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return timeo;
}

int bt_sock_stream_recvmsg(struct kiocb *iocb, struct socket *sock,
			       struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int err = 0;
	size_t target, copied = 0;
	long timeo;

	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	BT_DBG("sk %p size %zu", sk, size);

	lock_sock(sk);

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, size);
	timeo  = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	do {
		struct sk_buff *skb;
		int chunk;

		skb = skb_dequeue(&sk->sk_receive_queue);
		if (!skb) {
			if (copied >= target)
				break;

			err = sock_error(sk);
			if (err)
				break;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			err = -EAGAIN;
			if (!timeo)
				break;

			timeo = bt_sock_data_wait(sk, timeo);

			if (signal_pending(current)) {
				err = sock_intr_errno(timeo);
				goto out;
			}
			continue;
		}

		chunk = min_t(unsigned int, skb->len, size);
		if (memcpy_toiovec(msg->msg_iov, skb->data, chunk)) {
			skb_queue_head(&sk->sk_receive_queue, skb);
			if (!copied)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size   -= chunk;

		sock_recv_ts_and_drops(msg, sk, skb);

		if (!(flags & MSG_PEEK)) {
			skb_pull(skb, chunk);
			if (skb->len) {
				skb_queue_head(&sk->sk_receive_queue, skb);
				break;
			}
			kfree_skb(skb);

		} else {
			/* put message back and return */
			skb_queue_head(&sk->sk_receive_queue, skb);
			break;
		}
	} while (size);

out:
	release_sock(sk);
	return copied ? : err;
}
EXPORT_SYMBOL(bt_sock_stream_recvmsg);

static inline unsigned int bt_accept_poll(struct sock *parent)
{
	struct bt_sock *bt = bt_sk(parent);
	struct bt_sock *s;
	struct sock *sk;
	unsigned int mask = 0;

	spin_lock_bh(&bt->accept_q_lock);
	list_for_each_entry(s, &bt->accept_q, accept_q) {
		int state;

		sk = (struct sock *)s;
		state = ACCESS_ONCE(sk->sk_state);

		if (state == BT_CONNECTED ||
		    (bt->defer_setup && state == BT_CONNECT2)) {
			mask = POLLIN | POLLRDNORM;
			break;
		}
	}
	spin_unlock_bh(&bt->accept_q_lock);

	return mask;
}

unsigned int bt_sock_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	poll_wait(file, sk_sleep(sk), wait);

	if (sk->sk_state == BT_LISTEN)
		return bt_accept_poll(sk);

	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP | POLLIN | POLLRDNORM;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == BT_CLOSED)
		mask |= POLLHUP;

	if (sk->sk_state == BT_CONNECT ||
			sk->sk_state == BT_CONNECT2 ||
			sk->sk_state == BT_CONFIG)
		return mask;

	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}
EXPORT_SYMBOL(bt_sock_poll);

int bt_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	long amount;
	int err;

	BT_DBG("sk %p cmd %x arg %lx", sk, cmd, arg);

	switch (cmd) {
	case TIOCOUTQ:
		if (sk->sk_state == BT_LISTEN)
			return -EINVAL;

		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		err = put_user(amount, (int __user *) arg);
		break;

	case TIOCINQ:
		if (sk->sk_state == BT_LISTEN)
			return -EINVAL;

		lock_sock(sk);
		skb = skb_peek(&sk->sk_receive_queue);
		amount = skb ? skb->len : 0;
		release_sock(sk);
		err = put_user(amount, (int __user *) arg);
		break;

	case SIOCGSTAMP:
		err = sock_get_timestamp(sk, (struct timeval __user *) arg);
		break;

	case SIOCGSTAMPNS:
		err = sock_get_timestampns(sk, (struct timespec __user *) arg);
		break;

	default:
		err = -ENOIOCTLCMD;
		break;
	}

	return err;
}
EXPORT_SYMBOL(bt_sock_ioctl);

int bt_sock_wait_state(struct sock *sk, int state, unsigned long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	BT_DBG("sk %p", sk);

	add_wait_queue(sk_sleep(sk), &wait);
	while (sk->sk_state != state) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!timeo) {
			err = -EINPROGRESS;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		err = sock_error(sk);
		if (err)
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
}
EXPORT_SYMBOL(bt_sock_wait_state);

static struct net_proto_family bt_sock_family_ops = {
	.owner	= THIS_MODULE,
	.family	= PF_BLUETOOTH,
	.create	= bt_sock_create,
};

static int __init bt_init(void)
{
	int err;

	BT_INFO("Core ver %s", VERSION);

	err = bt_sysfs_init();
	if (err < 0)
		return err;

	err = sock_register(&bt_sock_family_ops);
	if (err < 0) {
		bt_sysfs_cleanup();
		return err;
	}

	BT_INFO("HCI device and connection manager initialized");

	err = hci_sock_init();
	if (err < 0)
		goto error;

	err = l2cap_init();
	if (err < 0)
		goto sock_err;

	err = sco_init();
	if (err < 0) {
		l2cap_exit();
		goto sock_err;
	}

	return 0;

sock_err:
	hci_sock_cleanup();

error:
	sock_unregister(PF_BLUETOOTH);
	bt_sysfs_cleanup();

	return err;
}

static void __exit bt_exit(void)
{

	sco_exit();

	l2cap_exit();

	hci_sock_cleanup();

	sock_unregister(PF_BLUETOOTH);

	bt_sysfs_cleanup();
}

subsys_initcall(bt_init);
module_exit(bt_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth Core ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_BLUETOOTH);
