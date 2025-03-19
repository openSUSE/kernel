// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/rtnetlink.h>

static const struct net_device_ops ovpn_netdev_ops = {
};

/**
 * ovpn_dev_is_valid - check if the netdevice is of type 'ovpn'
 * @dev: the interface to check
 *
 * Return: whether the netdevice is of type 'ovpn'
 */
static bool ovpn_dev_is_valid(const struct net_device *dev)
{
	return dev->netdev_ops == &ovpn_netdev_ops;
}

static int ovpn_newlink(struct net *src_net,
			struct net_device *dev,
			struct nlattr *tb[],
			struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static struct rtnl_link_ops ovpn_link_ops = {
	.kind = "ovpn",
	.netns_refund = false,
	.newlink = ovpn_newlink,
	.dellink = unregister_netdevice_queue,
};

static int ovpn_netdev_notifier_call(struct notifier_block *nb,
				     unsigned long state, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (!ovpn_dev_is_valid(dev))
		return NOTIFY_DONE;

	switch (state) {
	case NETDEV_REGISTER:
		/* add device to internal list for later destruction upon
		 * unregistration
		 */
		break;
	case NETDEV_UNREGISTER:
		/* can be delivered multiple times, so check registered flag,
		 * then destroy the interface
		 */
		break;
	case NETDEV_POST_INIT:
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:
	case NETDEV_UP:
	case NETDEV_PRE_UP:
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block ovpn_netdev_notifier = {
	.notifier_call = ovpn_netdev_notifier_call,
};

static int __init ovpn_init(void)
{
	int err = register_netdevice_notifier(&ovpn_netdev_notifier);

	if (err) {
		pr_err("ovpn: can't register netdevice notifier: %d\n", err);
		return err;
	}

	err = rtnl_link_register(&ovpn_link_ops);
	if (err) {
		pr_err("ovpn: can't register rtnl link ops: %d\n", err);
		goto unreg_netdev;
	}

	return 0;

unreg_netdev:
	unregister_netdevice_notifier(&ovpn_netdev_notifier);
	return err;
}

static __exit void ovpn_cleanup(void)
{
	rtnl_link_unregister(&ovpn_link_ops);
	unregister_netdevice_notifier(&ovpn_netdev_notifier);

	rcu_barrier();
}

module_init(ovpn_init);
module_exit(ovpn_cleanup);

MODULE_DESCRIPTION("OpenVPN data channel offload (ovpn)");
MODULE_AUTHOR("(C) 2020-2025 OpenVPN, Inc.");
MODULE_LICENSE("GPL");
