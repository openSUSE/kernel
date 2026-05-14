// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/netdev_queues.h>

#include "eea_adminq.h"
#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

#define EEA_SPLIT_HDR_SIZE ALIGN(128, L1_CACHE_BYTES)

static int eea_update_cfg(struct eea_net *enet,
			  struct eea_device *edev,
			  struct eea_aq_cfg *hwcfg)
{
	u32 rx_max = le32_to_cpu(hwcfg->rx_depth_max);
	u32 tx_max = le32_to_cpu(hwcfg->tx_depth_max);
	u32 rx_def = le32_to_cpu(hwcfg->rx_depth_def);
	u32 tx_def = le32_to_cpu(hwcfg->tx_depth_def);

	/* Now, we assert that the rx ring num is equal to the tx ring num. */
	if (edev->rx_num != edev->tx_num) {
		dev_err(edev->dma_dev, "Inconsistent ring num: RX %u, TX %u\n",
			edev->rx_num, edev->tx_num);
		return -EINVAL;
	}

	if (rx_max > EEA_NET_IO_HW_RING_DEPTH_MAX ||
	    rx_max < EEA_NET_IO_HW_RING_DEPTH_MIN ||
	    tx_max > EEA_NET_IO_HW_RING_DEPTH_MAX ||
	    tx_max < EEA_NET_IO_HW_RING_DEPTH_MIN) {
		dev_err(edev->dma_dev, "Invalid HW max depth: RX %u, TX %u\n",
			rx_max, tx_max);
		return -EINVAL;
	}

	if (rx_def > rx_max ||
	    tx_def > tx_max ||
	    rx_def < EEA_NET_IO_HW_RING_DEPTH_MIN ||
	    tx_def < EEA_NET_IO_HW_RING_DEPTH_MIN) {
		dev_err(edev->dma_dev, "Invalid default depth: RX %u (max %u), TX %u (max %u)\n",
			rx_def, rx_max, tx_def, tx_max);
		return -EINVAL;
	}

	if (!is_power_of_2(rx_max) || !is_power_of_2(tx_max) ||
	    !is_power_of_2(rx_def) || !is_power_of_2(tx_def)) {
		dev_err(edev->dma_dev, "Ring depth must be power of 2\n");
		return -EINVAL;
	}

	enet->cfg_hw.rx_ring_depth = rx_max;
	enet->cfg_hw.tx_ring_depth = tx_max;
	enet->cfg_hw.rx_ring_num = edev->rx_num;
	enet->cfg_hw.tx_ring_num = edev->tx_num;
	enet->cfg_hw.split_hdr = EEA_SPLIT_HDR_SIZE;

	enet->cfg.rx_ring_depth = rx_def;
	enet->cfg.tx_ring_depth = tx_def;
	enet->cfg.rx_ring_num = edev->rx_num;
	enet->cfg.tx_ring_num = edev->tx_num;

	return 0;
}

static int eea_netdev_init_features(struct net_device *netdev,
				    struct eea_net *enet,
				    struct eea_device *edev)
{
	struct eea_aq_cfg *cfg;
	int err;
	u32 mtu;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	err = eea_adminq_query_cfg(enet, cfg);
	if (err)
		goto err_free;

	mtu = le16_to_cpu(cfg->mtu);
	if (mtu < ETH_MIN_MTU) {
		dev_err(edev->dma_dev, "The device gave us an invalid MTU. Here we can only exit the initialization. %u < %u\n",
			mtu, ETH_MIN_MTU);
		err = -EINVAL;
		goto err_free;
	}

	err = eea_update_cfg(enet, edev, cfg);
	if (err)
		goto err_free;

	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	netdev->hw_features |= NETIF_F_HW_CSUM;
	netdev->hw_features |= NETIF_F_GRO_HW;
	netdev->hw_features |= NETIF_F_SG;
	netdev->hw_features |= NETIF_F_TSO;
	netdev->hw_features |= NETIF_F_TSO_ECN;
	netdev->hw_features |= NETIF_F_TSO6;
	netdev->hw_features |= NETIF_F_GSO_UDP_L4;

	netdev->features |= NETIF_F_HIGHDMA;
	netdev->features |= NETIF_F_HW_CSUM;
	netdev->features |= NETIF_F_SG;
	netdev->features |= NETIF_F_GSO_ROBUST;
	netdev->features |= netdev->hw_features & NETIF_F_ALL_TSO;
	netdev->features |= NETIF_F_RXCSUM;
	netdev->features |= NETIF_F_GRO_HW;

	netdev->vlan_features = netdev->features;

	if (!is_valid_ether_addr(cfg->mac)) {
		dev_err(edev->dma_dev, "The device gave invalid mac %pM\n",
			cfg->mac);
		err = -EINVAL;
		goto err_free;
	}

	eth_hw_addr_set(netdev, cfg->mac);

	enet->speed = SPEED_UNKNOWN;
	enet->duplex = DUPLEX_UNKNOWN;

	netdev->min_mtu = ETH_MIN_MTU;

	netdev->mtu = mtu;

	/* If jumbo frames are already enabled, then the returned MTU will be a
	 * jumbo MTU, and the driver will automatically enable jumbo frame
	 * support by default.
	 */
	netdev->max_mtu = mtu;

err_free:
	kfree(cfg);
	return err;
}

static const struct net_device_ops eea_netdev = {
	.ndo_validate_addr  = eth_validate_addr,
	.ndo_features_check = passthru_features_check,
};

static struct eea_net *eea_netdev_alloc(struct eea_device *edev, u32 pairs)
{
	struct net_device *netdev;
	struct eea_net *enet;

	netdev = alloc_etherdev_mq(sizeof(struct eea_net), pairs);
	if (!netdev) {
		dev_err(edev->dma_dev,
			"alloc_etherdev_mq failed with pairs %d\n", pairs);
		return NULL;
	}

	netdev->netdev_ops = &eea_netdev;
	SET_NETDEV_DEV(netdev, edev->dma_dev);

	enet = netdev_priv(netdev);
	enet->netdev = netdev;
	enet->edev = edev;
	edev->enet = enet;

	return enet;
}

int eea_net_probe(struct eea_device *edev)
{
	struct eea_net *enet;
	int err = -ENOMEM;

	enet = eea_netdev_alloc(edev, edev->rx_num);
	if (!enet)
		return -ENOMEM;

	err = eea_create_adminq(enet, edev->rx_num + edev->tx_num);
	if (err)
		goto err_free_netdev;

	eea_adminq_config_host_info(enet);

	err = eea_netdev_init_features(enet->netdev, enet, edev);
	if (err)
		goto err_reset_dev;

	netdev_dbg(enet->netdev, "eea probe success.\n");

	/* Queue TX/RX implementation is still in progress. register_netdev is
	 * deferred until these are completed in subsequent commits.
	 */

	return 0;

err_reset_dev:
	eea_device_reset(edev);
	eea_destroy_adminq(enet);

err_free_netdev:
	free_netdev(enet->netdev);
	return err;
}

void eea_net_remove(struct eea_device *edev)
{
	struct net_device *netdev;
	struct eea_net *enet;

	enet = edev->enet;
	netdev = enet->netdev;

	netdev_dbg(enet->netdev, "eea removed.\n");

	eea_device_reset(edev);

	eea_destroy_adminq(enet);

	free_netdev(netdev);
}

void eea_net_shutdown(struct eea_device *edev)
{
	struct net_device *netdev;
	struct eea_net *enet;

	enet = edev->enet;
	netdev = enet->netdev;

	rtnl_lock();

	netif_device_detach(netdev);

	eea_device_reset(edev);

	eea_destroy_adminq(enet);

	rtnl_unlock();
}
