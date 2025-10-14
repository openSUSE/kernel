// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common framework for low-level network console, dump, and debugger code
 *
 * Sep 8 2003  Matt Mackall <mpm@selenic.com>
 *
 * based on the netconsole code from:
 *
 * Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002  Red Hat, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/netpoll.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/if_vlan.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/addrconf.h>
#include <net/ndisc.h>
#include <net/ip6_checksum.h>
#include <linux/unaligned.h>
#include <trace/events/napi.h>
#include <linux/kconfig.h>

/*
 * We maintain a small pool of fully-sized skbs, to make sure the
 * message gets out even in extreme OOM situations.
 */

#define MAX_UDP_CHUNK 1460
#define MAX_SKBS 32
#define USEC_PER_POLL	50

#define MAX_SKB_SIZE							\
	(sizeof(struct ethhdr) +					\
	 sizeof(struct iphdr) +						\
	 sizeof(struct udphdr) +					\
	 MAX_UDP_CHUNK)

static void zap_completion_queue(void);

static unsigned int carrier_timeout = 4;
module_param(carrier_timeout, uint, 0644);

static netdev_tx_t netpoll_start_xmit(struct sk_buff *skb,
				      struct net_device *dev,
				      struct netdev_queue *txq)
{
	netdev_tx_t status = NETDEV_TX_OK;
	netdev_features_t features;

	features = netif_skb_features(skb);

	if (skb_vlan_tag_present(skb) &&
	    !vlan_hw_offload_capable(features, skb->vlan_proto)) {
		skb = __vlan_hwaccel_push_inside(skb);
		if (unlikely(!skb)) {
			/* This is actually a packet drop, but we
			 * don't want the code that calls this
			 * function to try and operate on a NULL skb.
			 */
			goto out;
		}
	}

	status = netdev_start_xmit(skb, dev, txq, false);

out:
	return status;
}

static void queue_process(struct work_struct *work)
{
	struct netpoll_info *npinfo =
		container_of(work, struct netpoll_info, tx_work.work);
	struct sk_buff *skb;
	unsigned long flags;

	while ((skb = skb_dequeue(&npinfo->txq))) {
		struct net_device *dev = skb->dev;
		struct netdev_queue *txq;
		unsigned int q_index;

		if (!netif_device_present(dev) || !netif_running(dev)) {
			kfree_skb(skb);
			continue;
		}

		local_irq_save(flags);
		/* check if skb->queue_mapping is still valid */
		q_index = skb_get_queue_mapping(skb);
		if (unlikely(q_index >= dev->real_num_tx_queues)) {
			q_index = q_index % dev->real_num_tx_queues;
			skb_set_queue_mapping(skb, q_index);
		}
		txq = netdev_get_tx_queue(dev, q_index);
		HARD_TX_LOCK(dev, txq, smp_processor_id());
		if (netif_xmit_frozen_or_stopped(txq) ||
		    !dev_xmit_complete(netpoll_start_xmit(skb, dev, txq))) {
			skb_queue_head(&npinfo->txq, skb);
			HARD_TX_UNLOCK(dev, txq);
			local_irq_restore(flags);

			schedule_delayed_work(&npinfo->tx_work, HZ/10);
			return;
		}
		HARD_TX_UNLOCK(dev, txq);
		local_irq_restore(flags);
	}
}

static int netif_local_xmit_active(struct net_device *dev)
{
	int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		if (READ_ONCE(txq->xmit_lock_owner) == smp_processor_id())
			return 1;
	}

	return 0;
}

static void poll_one_napi(struct napi_struct *napi)
{
	int work;

	/* If we set this bit but see that it has already been set,
	 * that indicates that napi has been disabled and we need
	 * to abort this operation
	 */
	if (test_and_set_bit(NAPI_STATE_NPSVC, &napi->state))
		return;

	/* We explicitly pass the polling call a budget of 0 to
	 * indicate that we are clearing the Tx path only.
	 */
	work = napi->poll(napi, 0);
	WARN_ONCE(work, "%pS exceeded budget in poll\n", napi->poll);
	trace_napi_poll(napi, work, 0);

	clear_bit(NAPI_STATE_NPSVC, &napi->state);
}

static void poll_napi(struct net_device *dev)
{
	struct napi_struct *napi;
	int cpu = smp_processor_id();

	list_for_each_entry_rcu(napi, &dev->napi_list, dev_list) {
		if (cmpxchg(&napi->poll_owner, -1, cpu) == -1) {
			poll_one_napi(napi);
			smp_store_release(&napi->poll_owner, -1);
		}
	}
}

void netpoll_poll_dev(struct net_device *dev)
{
	struct netpoll_info *ni = rcu_dereference_bh(dev->npinfo);
	const struct net_device_ops *ops;

	/* Don't do any rx activity if the dev_lock mutex is held
	 * the dev_open/close paths use this to block netpoll activity
	 * while changing device state
	 */
	if (!ni || down_trylock(&ni->dev_lock))
		return;

	/* Some drivers will take the same locks in poll and xmit,
	 * we can't poll if local CPU is already in xmit.
	 */
	if (!netif_running(dev) || netif_local_xmit_active(dev)) {
		up(&ni->dev_lock);
		return;
	}

	ops = dev->netdev_ops;
	if (ops->ndo_poll_controller)
		ops->ndo_poll_controller(dev);

	poll_napi(dev);

	up(&ni->dev_lock);

	zap_completion_queue();
}
EXPORT_SYMBOL(netpoll_poll_dev);

void netpoll_poll_disable(struct net_device *dev)
{
	struct netpoll_info *ni;

	might_sleep();
	ni = rtnl_dereference(dev->npinfo);
	if (ni)
		down(&ni->dev_lock);
}

void netpoll_poll_enable(struct net_device *dev)
{
	struct netpoll_info *ni;

	ni = rtnl_dereference(dev->npinfo);
	if (ni)
		up(&ni->dev_lock);
}

static void refill_skbs(struct netpoll *np)
{
	struct sk_buff_head *skb_pool;
	struct sk_buff *skb;
	unsigned long flags;

	skb_pool = &np->skb_pool;

	spin_lock_irqsave(&skb_pool->lock, flags);
	while (skb_pool->qlen < MAX_SKBS) {
		skb = alloc_skb(MAX_SKB_SIZE, GFP_ATOMIC);
		if (!skb)
			break;

		__skb_queue_tail(skb_pool, skb);
	}
	spin_unlock_irqrestore(&skb_pool->lock, flags);
}

static void zap_completion_queue(void)
{
	unsigned long flags;
	struct softnet_data *sd = &get_cpu_var(softnet_data);

	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_save(flags);
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_restore(flags);

		while (clist != NULL) {
			struct sk_buff *skb = clist;
			clist = clist->next;
			if (!skb_irq_freeable(skb)) {
				refcount_set(&skb->users, 1);
				dev_kfree_skb_any(skb); /* put this one back */
			} else {
				__kfree_skb(skb);
			}
		}
	}

	put_cpu_var(softnet_data);
}

static struct sk_buff *find_skb(struct netpoll *np, int len, int reserve)
{
	int count = 0;
	struct sk_buff *skb;

	zap_completion_queue();
repeat:

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		skb = skb_dequeue(&np->skb_pool);
		schedule_work(&np->refill_wq);
	}

	if (!skb) {
		if (++count < 10) {
			netpoll_poll_dev(np->dev);
			goto repeat;
		}
		return NULL;
	}

	refcount_set(&skb->users, 1);
	skb_reserve(skb, reserve);
	return skb;
}

static int netpoll_owner_active(struct net_device *dev)
{
	struct napi_struct *napi;

	list_for_each_entry_rcu(napi, &dev->napi_list, dev_list) {
		if (READ_ONCE(napi->poll_owner) == smp_processor_id())
			return 1;
	}
	return 0;
}

/* call with IRQ disabled */
static netdev_tx_t __netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	netdev_tx_t status = NETDEV_TX_BUSY;
	netdev_tx_t ret = NET_XMIT_DROP;
	struct net_device *dev;
	unsigned long tries;
	/* It is up to the caller to keep npinfo alive. */
	struct netpoll_info *npinfo;

	lockdep_assert_irqs_disabled();

	dev = np->dev;
	rcu_read_lock();
	npinfo = rcu_dereference_bh(dev->npinfo);

	if (!npinfo || !netif_running(dev) || !netif_device_present(dev)) {
		dev_kfree_skb_irq(skb);
		goto out;
	}

	/* don't get messages out of order, and no recursion */
	if (skb_queue_len(&npinfo->txq) == 0 && !netpoll_owner_active(dev)) {
		struct netdev_queue *txq;

		txq = netdev_core_pick_tx(dev, skb, NULL);

		/* try until next clock tick */
		for (tries = jiffies_to_usecs(1)/USEC_PER_POLL;
		     tries > 0; --tries) {
			if (HARD_TX_TRYLOCK(dev, txq)) {
				if (!netif_xmit_stopped(txq))
					status = netpoll_start_xmit(skb, dev, txq);

				HARD_TX_UNLOCK(dev, txq);

				if (dev_xmit_complete(status))
					break;

			}

			/* tickle device maybe there is some cleanup */
			netpoll_poll_dev(np->dev);

			udelay(USEC_PER_POLL);
		}

		WARN_ONCE(!irqs_disabled(),
			"netpoll_send_skb_on_dev(): %s enabled interrupts in poll (%pS)\n",
			dev->name, dev->netdev_ops->ndo_start_xmit);

	}

	if (!dev_xmit_complete(status)) {
		skb_queue_tail(&npinfo->txq, skb);
		schedule_delayed_work(&npinfo->tx_work,0);
	}
	ret = NETDEV_TX_OK;
out:
	rcu_read_unlock();
	return ret;
}

static void netpoll_udp_checksum(struct netpoll *np, struct sk_buff *skb,
				 int len)
{
	struct udphdr *udph;
	int udp_len;

	udp_len = len + sizeof(struct udphdr);
	udph = udp_hdr(skb);

	/* check needs to be set, since it will be consumed in csum_partial */
	udph->check = 0;
	if (np->ipv6)
		udph->check = csum_ipv6_magic(&np->local_ip.in6,
					      &np->remote_ip.in6,
					      udp_len, IPPROTO_UDP,
					      csum_partial(udph, udp_len, 0));
	else
		udph->check = csum_tcpudp_magic(np->local_ip.ip,
						np->remote_ip.ip,
						udp_len, IPPROTO_UDP,
						csum_partial(udph, udp_len, 0));
	if (udph->check == 0)
		udph->check = CSUM_MANGLED_0;
}

netdev_tx_t netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	unsigned long flags;
	netdev_tx_t ret;

	if (unlikely(!np)) {
		dev_kfree_skb_irq(skb);
		ret = NET_XMIT_DROP;
	} else {
		local_irq_save(flags);
		ret = __netpoll_send_skb(np, skb);
		local_irq_restore(flags);
	}
	return ret;
}
EXPORT_SYMBOL(netpoll_send_skb);

static void push_ipv6(struct netpoll *np, struct sk_buff *skb, int len)
{
	struct ipv6hdr *ip6h;

	skb_push(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	ip6h = ipv6_hdr(skb);

	/* ip6h->version = 6; ip6h->priority = 0; */
	*(unsigned char *)ip6h = 0x60;
	ip6h->flow_lbl[0] = 0;
	ip6h->flow_lbl[1] = 0;
	ip6h->flow_lbl[2] = 0;

	ip6h->payload_len = htons(sizeof(struct udphdr) + len);
	ip6h->nexthdr = IPPROTO_UDP;
	ip6h->hop_limit = 32;
	ip6h->saddr = np->local_ip.in6;
	ip6h->daddr = np->remote_ip.in6;

	skb->protocol = htons(ETH_P_IPV6);
}

static void push_ipv4(struct netpoll *np, struct sk_buff *skb, int len)
{
	static atomic_t ip_ident;
	struct iphdr *iph;
	int ip_len;

	ip_len = len + sizeof(struct udphdr) + sizeof(struct iphdr);

	skb_push(skb, sizeof(struct iphdr));
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);

	/* iph->version = 4; iph->ihl = 5; */
	*(unsigned char *)iph = 0x45;
	iph->tos = 0;
	put_unaligned(htons(ip_len), &iph->tot_len);
	iph->id = htons(atomic_inc_return(&ip_ident));
	iph->frag_off = 0;
	iph->ttl = 64;
	iph->protocol = IPPROTO_UDP;
	iph->check = 0;
	put_unaligned(np->local_ip.ip, &iph->saddr);
	put_unaligned(np->remote_ip.ip, &iph->daddr);
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	skb->protocol = htons(ETH_P_IP);
}

static void push_udp(struct netpoll *np, struct sk_buff *skb, int len)
{
	struct udphdr *udph;
	int udp_len;

	udp_len = len + sizeof(struct udphdr);

	skb_push(skb, sizeof(struct udphdr));
	skb_reset_transport_header(skb);

	udph = udp_hdr(skb);
	udph->source = htons(np->local_port);
	udph->dest = htons(np->remote_port);
	udph->len = htons(udp_len);

	netpoll_udp_checksum(np, skb, len);
}

static void push_eth(struct netpoll *np, struct sk_buff *skb)
{
	struct ethhdr *eth;

	eth = skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	ether_addr_copy(eth->h_source, np->dev->dev_addr);
	ether_addr_copy(eth->h_dest, np->remote_mac);
	if (np->ipv6)
		eth->h_proto = htons(ETH_P_IPV6);
	else
		eth->h_proto = htons(ETH_P_IP);
}

int netpoll_send_udp(struct netpoll *np, const char *msg, int len)
{
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		WARN_ON_ONCE(!irqs_disabled());

	udp_len = len + sizeof(struct udphdr);
	if (np->ipv6)
		ip_len = udp_len + sizeof(struct ipv6hdr);
	else
		ip_len = udp_len + sizeof(struct iphdr);

	total_len = ip_len + LL_RESERVED_SPACE(np->dev);

	skb = find_skb(np, total_len + np->dev->needed_tailroom,
		       total_len - len);
	if (!skb)
		return -ENOMEM;

	skb_copy_to_linear_data(skb, msg, len);
	skb_put(skb, len);

	push_udp(np, skb, len);
	if (np->ipv6)
		push_ipv6(np, skb, len);
	else
		push_ipv4(np, skb, len);
	push_eth(np, skb);
	skb->dev = np->dev;

	return (int)netpoll_send_skb(np, skb);
}
EXPORT_SYMBOL(netpoll_send_udp);


static void skb_pool_flush(struct netpoll *np)
{
	struct sk_buff_head *skb_pool;

	cancel_work_sync(&np->refill_wq);
	skb_pool = &np->skb_pool;
	skb_queue_purge_reason(skb_pool, SKB_CONSUMED);
}

static void refill_skbs_work_handler(struct work_struct *work)
{
	struct netpoll *np =
		container_of(work, struct netpoll, refill_wq);

	refill_skbs(np);
}

int __netpoll_setup(struct netpoll *np, struct net_device *ndev)
{
	struct netpoll_info *npinfo;
	const struct net_device_ops *ops;
	int err;

	skb_queue_head_init(&np->skb_pool);

	if (ndev->priv_flags & IFF_DISABLE_NETPOLL) {
		np_err(np, "%s doesn't support polling, aborting\n",
		       ndev->name);
		err = -ENOTSUPP;
		goto out;
	}

	npinfo = rtnl_dereference(ndev->npinfo);
	if (!npinfo) {
		npinfo = kmalloc(sizeof(*npinfo), GFP_KERNEL);
		if (!npinfo) {
			err = -ENOMEM;
			goto out;
		}

		sema_init(&npinfo->dev_lock, 1);
		skb_queue_head_init(&npinfo->txq);
		INIT_DELAYED_WORK(&npinfo->tx_work, queue_process);

		refcount_set(&npinfo->refcnt, 1);

		ops = ndev->netdev_ops;
		if (ops->ndo_netpoll_setup) {
			err = ops->ndo_netpoll_setup(ndev);
			if (err)
				goto free_npinfo;
		}
	} else {
		refcount_inc(&npinfo->refcnt);
	}

	np->dev = ndev;
	strscpy(np->dev_name, ndev->name, IFNAMSIZ);

	/* fill up the skb queue */
	refill_skbs(np);
	INIT_WORK(&np->refill_wq, refill_skbs_work_handler);

	/* last thing to do is link it to the net device structure */
	rcu_assign_pointer(ndev->npinfo, npinfo);

	return 0;

free_npinfo:
	kfree(npinfo);
out:
	return err;
}
EXPORT_SYMBOL_GPL(__netpoll_setup);

/*
 * Returns a pointer to a string representation of the identifier used
 * to select the egress interface for the given netpoll instance. buf
 * must be a buffer of length at least MAC_ADDR_STR_LEN + 1.
 */
static char *egress_dev(struct netpoll *np, char *buf)
{
	if (np->dev_name[0])
		return np->dev_name;

	snprintf(buf, MAC_ADDR_STR_LEN, "%pM", np->dev_mac);
	return buf;
}

static void netpoll_wait_carrier(struct netpoll *np, struct net_device *ndev,
				 unsigned int timeout)
{
	unsigned long atmost;

	atmost = jiffies + timeout * HZ;
	while (!netif_carrier_ok(ndev)) {
		if (time_after(jiffies, atmost)) {
			np_notice(np, "timeout waiting for carrier\n");
			break;
		}
		msleep(1);
	}
}

/*
 * Take the IPv6 from ndev and populate local_ip structure in netpoll
 */
static int netpoll_take_ipv6(struct netpoll *np, struct net_device *ndev)
{
	char buf[MAC_ADDR_STR_LEN + 1];
	int err = -EDESTADDRREQ;
	struct inet6_dev *idev;

	if (!IS_ENABLED(CONFIG_IPV6)) {
		np_err(np, "IPv6 is not supported %s, aborting\n",
		       egress_dev(np, buf));
		return -EINVAL;
	}

	idev = __in6_dev_get(ndev);
	if (idev) {
		struct inet6_ifaddr *ifp;

		read_lock_bh(&idev->lock);
		list_for_each_entry(ifp, &idev->addr_list, if_list) {
			if (!!(ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL) !=
				!!(ipv6_addr_type(&np->remote_ip.in6) & IPV6_ADDR_LINKLOCAL))
				continue;
			/* Got the IP, let's return */
			np->local_ip.in6 = ifp->addr;
			err = 0;
			break;
		}
		read_unlock_bh(&idev->lock);
	}
	if (err) {
		np_err(np, "no IPv6 address for %s, aborting\n",
		       egress_dev(np, buf));
		return err;
	}

	np_info(np, "local IPv6 %pI6c\n", &np->local_ip.in6);
	return 0;
}

/*
 * Take the IPv4 from ndev and populate local_ip structure in netpoll
 */
static int netpoll_take_ipv4(struct netpoll *np, struct net_device *ndev)
{
	char buf[MAC_ADDR_STR_LEN + 1];
	const struct in_ifaddr *ifa;
	struct in_device *in_dev;

	in_dev = __in_dev_get_rtnl(ndev);
	if (!in_dev) {
		np_err(np, "no IP address for %s, aborting\n",
		       egress_dev(np, buf));
		return -EDESTADDRREQ;
	}

	ifa = rtnl_dereference(in_dev->ifa_list);
	if (!ifa) {
		np_err(np, "no IP address for %s, aborting\n",
		       egress_dev(np, buf));
		return -EDESTADDRREQ;
	}

	np->local_ip.ip = ifa->ifa_local;
	np_info(np, "local IP %pI4\n", &np->local_ip.ip);

	return 0;
}

int netpoll_setup(struct netpoll *np)
{
	struct net *net = current->nsproxy->net_ns;
	char buf[MAC_ADDR_STR_LEN + 1];
	struct net_device *ndev = NULL;
	bool ip_overwritten = false;
	int err;

	rtnl_lock();
	if (np->dev_name[0])
		ndev = __dev_get_by_name(net, np->dev_name);
	else if (is_valid_ether_addr(np->dev_mac))
		ndev = dev_getbyhwaddr(net, ARPHRD_ETHER, np->dev_mac);

	if (!ndev) {
		np_err(np, "%s doesn't exist, aborting\n", egress_dev(np, buf));
		err = -ENODEV;
		goto unlock;
	}
	netdev_hold(ndev, &np->dev_tracker, GFP_KERNEL);

	if (netdev_master_upper_dev_get(ndev)) {
		np_err(np, "%s is a slave device, aborting\n",
		       egress_dev(np, buf));
		err = -EBUSY;
		goto put;
	}

	if (!netif_running(ndev)) {
		np_info(np, "device %s not up yet, forcing it\n",
			egress_dev(np, buf));

		err = dev_open(ndev, NULL);
		if (err) {
			np_err(np, "failed to open %s\n", ndev->name);
			goto put;
		}

		rtnl_unlock();
		netpoll_wait_carrier(np, ndev, carrier_timeout);
		rtnl_lock();
	}

	if (!np->local_ip.ip) {
		if (!np->ipv6) {
			err = netpoll_take_ipv4(np, ndev);
			if (err)
				goto put;
		} else {
			err = netpoll_take_ipv6(np, ndev);
			if (err)
				goto put;
		}
		ip_overwritten = true;
	}

	err = __netpoll_setup(np, ndev);
	if (err)
		goto flush;
	rtnl_unlock();

	/* Make sure all NAPI polls which started before dev->npinfo
	 * was visible have exited before we start calling NAPI poll.
	 * NAPI skips locking if dev->npinfo is NULL.
	 */
	synchronize_rcu();

	return 0;

flush:
	skb_pool_flush(np);
put:
	DEBUG_NET_WARN_ON_ONCE(np->dev);
	if (ip_overwritten)
		memset(&np->local_ip, 0, sizeof(np->local_ip));
	netdev_put(ndev, &np->dev_tracker);
unlock:
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(netpoll_setup);

static void rcu_cleanup_netpoll_info(struct rcu_head *rcu_head)
{
	struct netpoll_info *npinfo =
			container_of(rcu_head, struct netpoll_info, rcu);

	skb_queue_purge(&npinfo->txq);

	/* we can't call cancel_delayed_work_sync here, as we are in softirq */
	cancel_delayed_work(&npinfo->tx_work);

	/* clean after last, unfinished work */
	__skb_queue_purge(&npinfo->txq);
	/* now cancel it again */
	cancel_delayed_work(&npinfo->tx_work);
	kfree(npinfo);
}

static void __netpoll_cleanup(struct netpoll *np)
{
	struct netpoll_info *npinfo;

	npinfo = rtnl_dereference(np->dev->npinfo);
	if (!npinfo)
		return;

	if (refcount_dec_and_test(&npinfo->refcnt)) {
		const struct net_device_ops *ops;

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_cleanup)
			ops->ndo_netpoll_cleanup(np->dev);

		RCU_INIT_POINTER(np->dev->npinfo, NULL);
		call_rcu(&npinfo->rcu, rcu_cleanup_netpoll_info);
	} else
		RCU_INIT_POINTER(np->dev->npinfo, NULL);

	skb_pool_flush(np);
}

void __netpoll_free(struct netpoll *np)
{
	ASSERT_RTNL();

	/* Wait for transmitting packets to finish before freeing. */
	synchronize_net();
	__netpoll_cleanup(np);
	kfree(np);
}
EXPORT_SYMBOL_GPL(__netpoll_free);

void do_netpoll_cleanup(struct netpoll *np)
{
	__netpoll_cleanup(np);
	netdev_put(np->dev, &np->dev_tracker);
	np->dev = NULL;
}
EXPORT_SYMBOL(do_netpoll_cleanup);

void netpoll_cleanup(struct netpoll *np)
{
	rtnl_lock();
	if (!np->dev)
		goto out;
	do_netpoll_cleanup(np);
out:
	rtnl_unlock();
}
EXPORT_SYMBOL(netpoll_cleanup);
