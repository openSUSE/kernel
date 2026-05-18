// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC switch driver
 * Copyright 2025-2026 NXP
 */

#include <linux/etherdevice.h>
#include <linux/fsl/enetc_mdio.h>
#include <linux/if_vlan.h>
#include <linux/of_mdio.h>

#include "netc_switch.h"

static enum dsa_tag_protocol
netc_get_tag_protocol(struct dsa_switch *ds, int port,
		      enum dsa_tag_protocol mprot)
{
	return DSA_TAG_PROTO_NETC;
}

static void netc_port_rmw(struct netc_port *np, u32 reg,
			  u32 mask, u32 val)
{
	u32 old, new;

	WARN_ON((mask | val) != mask);

	old = netc_port_rd(np, reg);
	new = (old & ~mask) | val;
	if (new == old)
		return;

	netc_port_wr(np, reg, new);
}

static void netc_mac_port_wr(struct netc_port *np, u32 reg, u32 val)
{
	if (is_netc_pseudo_port(np))
		return;

	netc_port_wr(np, reg, val);
	if (np->caps.pmac)
		netc_port_wr(np, reg + NETC_PMAC_OFFSET, val);
}

/* netc_mac_port_rmw() is used to synchronize the configurations of eMAC
 * and pMAC to maintain consistency. This function should not be used if
 * differentiated settings are required.
 */
static void netc_mac_port_rmw(struct netc_port *np, u32 reg,
			      u32 mask, u32 val)
{
	u32 old, new;

	if (is_netc_pseudo_port(np))
		return;

	WARN_ON((mask | val) != mask);

	old = netc_port_rd(np, reg);
	new = (old & ~mask) | val;
	if (new == old)
		return;

	netc_port_wr(np, reg, new);
	if (np->caps.pmac)
		netc_port_wr(np, reg + NETC_PMAC_OFFSET, new);
}

static void netc_port_get_capability(struct netc_port *np)
{
	u32 val;

	val = netc_port_rd(np, NETC_PMCAPR);
	if (val & PMCAPR_HD)
		np->caps.half_duplex = true;

	if (FIELD_GET(PMCAPR_FP, val) == FP_SUPPORT)
		np->caps.pmac = true;

	val = netc_port_rd(np, NETC_PCAPR);
	if (val & PCAPR_LINK_TYPE)
		np->caps.pseudo_link = true;
}

static int netc_port_create_emdio_bus(struct netc_port *np,
				      struct device_node *node)
{
	struct netc_switch *priv = np->switch_priv;
	struct enetc_mdio_priv *mdio_priv;
	struct device *dev = priv->dev;
	struct enetc_hw *hw;
	struct mii_bus *bus;
	int err;

	hw = enetc_hw_alloc(dev, np->iobase);
	if (IS_ERR(hw))
		return dev_err_probe(dev, PTR_ERR(hw),
				     "Failed to allocate enetc_hw\n");

	bus = devm_mdiobus_alloc_size(dev, sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "NXP NETC switch external MDIO Bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = hw;
	mdio_priv->mdio_base = NETC_EMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-p%d-emdio",
		 dev_name(dev), np->dp->index);

	err = devm_of_mdiobus_register(dev, bus, node);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot register EMDIO bus\n");

	np->emdio = bus;

	return 0;
}

static int netc_port_create_mdio_bus(struct netc_port *np,
				     struct device_node *node)
{
	struct device_node *mdio_node;
	int err;

	mdio_node = of_get_child_by_name(node, "mdio");
	if (mdio_node) {
		err = netc_port_create_emdio_bus(np, mdio_node);
		of_node_put(mdio_node);
		if (err)
			return err;
	}

	return 0;
}

static int netc_init_switch_id(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	struct dsa_switch *ds = priv->ds;

	/* The value of 0 is reserved for the VEPA switch and cannot
	 * be used. So 'dsa,member' is a required property for NETC
	 * switch, the member is used to specify the switch ID, which
	 * cannot be zero. This way, the hardware switch ID and the
	 * software switch ID are consistent.
	 */
	if (ds->index > FIELD_MAX(SWCR_SWID) || !ds->index) {
		dev_err(priv->dev, "Switch index %d out of range\n",
			ds->index);
		return -ERANGE;
	}

	netc_base_wr(regs, NETC_SWCR, ds->index);

	return 0;
}

static int netc_init_all_ports(struct netc_switch *priv)
{
	struct device *dev = priv->dev;
	struct netc_port *np;
	struct dsa_port *dp;
	int err;

	priv->ports = devm_kcalloc(dev, priv->info->num_ports,
				   sizeof(struct netc_port *),
				   GFP_KERNEL);
	if (!priv->ports)
		return -ENOMEM;

	/* Some DSA interfaces may set the port even it is disabled, such
	 * as .port_disable(), .port_stp_state_set() and so on. To avoid
	 * crash caused by accessing NULL port pointer, each port is
	 * allocated its own memory. Otherwise, we need to check whether
	 * the port pointer is NULL in these interfaces. The latter is
	 * difficult for us to cover.
	 */
	for (int i = 0; i < priv->info->num_ports; i++) {
		np = devm_kzalloc(dev, sizeof(*np), GFP_KERNEL);
		if (!np)
			return -ENOMEM;

		np->switch_priv = priv;
		np->iobase = priv->regs.port + PORT_IOBASE(i);
		netc_port_get_capability(np);
		priv->ports[i] = np;
	}

	dsa_switch_for_each_available_port(dp, priv->ds) {
		np = priv->ports[dp->index];
		np->dp = dp;

		if (dsa_port_is_user(dp)) {
			err = netc_port_create_mdio_bus(np, dp->dn);
			if (err) {
				dev_err(dev, "Failed to create MDIO bus\n");
				return err;
			}
		}
	}

	return 0;
}

static void netc_init_ntmp_tbl_versions(struct netc_switch *priv)
{
	struct ntmp_user *ntmp = &priv->ntmp;

	/* All tables default to version 0 */
	memset(&ntmp->tbl, 0, sizeof(ntmp->tbl));
}

static int netc_init_all_cbdrs(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	struct ntmp_user *ntmp = &priv->ntmp;
	int i, err;

	ntmp->cbdr_num = NETC_CBDR_NUM;
	ntmp->dev = priv->dev;
	ntmp->ring = devm_kcalloc(ntmp->dev, ntmp->cbdr_num,
				  sizeof(struct netc_cbdr),
				  GFP_KERNEL);
	if (!ntmp->ring)
		return -ENOMEM;

	for (i = 0; i < ntmp->cbdr_num; i++) {
		struct netc_cbdr *cbdr = &ntmp->ring[i];
		struct netc_cbdr_regs cbdr_regs;

		cbdr_regs.pir = regs->base + NETC_CBDRPIR(i);
		cbdr_regs.cir = regs->base + NETC_CBDRCIR(i);
		cbdr_regs.mr = regs->base + NETC_CBDRMR(i);
		cbdr_regs.bar0 = regs->base + NETC_CBDRBAR0(i);
		cbdr_regs.bar1 = regs->base + NETC_CBDRBAR1(i);
		cbdr_regs.lenr = regs->base + NETC_CBDRLENR(i);

		err = ntmp_init_cbdr(cbdr, ntmp->dev, &cbdr_regs);
		if (err)
			goto free_cbdrs;
	}

	return 0;

free_cbdrs:
	for (i--; i >= 0; i--)
		ntmp_free_cbdr(&ntmp->ring[i]);

	return err;
}

static void netc_remove_all_cbdrs(struct netc_switch *priv)
{
	struct ntmp_user *ntmp = &priv->ntmp;

	for (int i = 0; i < NETC_CBDR_NUM; i++)
		ntmp_free_cbdr(&ntmp->ring[i]);
}

static int netc_init_ntmp_user(struct netc_switch *priv)
{
	netc_init_ntmp_tbl_versions(priv);

	return netc_init_all_cbdrs(priv);
}

static void netc_free_ntmp_user(struct netc_switch *priv)
{
	netc_remove_all_cbdrs(priv);
}

static void netc_switch_dos_default_config(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val;

	val = DOSL2CR_SAMEADDR | DOSL2CR_MSAMCC;
	netc_base_wr(regs, NETC_DOSL2CR, val);

	val = DOSL3CR_SAMEADDR | DOSL3CR_IPSAMCC;
	netc_base_wr(regs, NETC_DOSL3CR, val);
}

static void netc_switch_vfht_default_config(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val;

	val = netc_base_rd(regs, NETC_VFHTDECR2);

	/* If no match is found in the VLAN Filter table, then VFHTDECR2[MLO]
	 * will take effect. VFHTDECR2[MLO] is set to "Software MAC learning
	 * secure" by default. Notice BPCR[MLO] will override VFHTDECR2[MLO]
	 * if its value is not zero.
	 */
	val = u32_replace_bits(val, MLO_SW_SEC, VFHTDECR2_MLO);
	val = u32_replace_bits(val, MFO_NO_MATCH_DISCARD, VFHTDECR2_MFO);
	netc_base_wr(regs, NETC_VFHTDECR2, val);
}

static void netc_port_set_max_frame_size(struct netc_port *np,
					 u32 max_frame_size)
{
	netc_mac_port_wr(np, NETC_PM_MAXFRM(0),
			 max_frame_size & PM_MAXFRAM);
}

static void netc_switch_fixed_config(struct netc_switch *priv)
{
	netc_switch_dos_default_config(priv);
	netc_switch_vfht_default_config(priv);
}

static void netc_port_set_tc_max_sdu(struct netc_port *np,
				     int tc, u32 max_sdu)
{
	u32 val = FIELD_PREP(PTCTMSDUR_MAXSDU, max_sdu) |
		  FIELD_PREP(PTCTMSDUR_SDU_TYPE, SDU_TYPE_MPDU);

	netc_port_wr(np, NETC_PTCTMSDUR(tc), val);
}

static void netc_port_set_all_tc_msdu(struct netc_port *np)
{
	for (int tc = 0; tc < NETC_TC_NUM; tc++)
		netc_port_set_tc_max_sdu(np, tc, NETC_MAX_FRAME_LEN);
}

static void netc_port_set_mlo(struct netc_port *np, enum netc_mlo mlo)
{
	netc_port_rmw(np, NETC_BPCR, BPCR_MLO, FIELD_PREP(BPCR_MLO, mlo));
}

static void netc_port_fixed_config(struct netc_port *np)
{
	/* Default IPV and DR setting */
	netc_port_rmw(np, NETC_PQOSMR, PQOSMR_VS | PQOSMR_VE,
		      PQOSMR_VS | PQOSMR_VE);

	/* Enable L2 and L3 DOS */
	netc_port_rmw(np, NETC_PCR, PCR_L2DOSE | PCR_L3DOSE,
		      PCR_L2DOSE | PCR_L3DOSE);
}

static void netc_port_default_config(struct netc_port *np)
{
	netc_port_fixed_config(np);

	/* Default VLAN unaware */
	netc_port_rmw(np, NETC_BPDVR, BPDVR_RXVAM, BPDVR_RXVAM);

	if (dsa_port_is_cpu(np->dp))
		/* For CPU port, source port pruning is disabled */
		netc_port_rmw(np, NETC_BPCR, BPCR_SRCPRND, BPCR_SRCPRND);
	else
		netc_port_set_mlo(np, MLO_DISABLE);

	netc_port_set_max_frame_size(np, NETC_MAX_FRAME_LEN);
	netc_port_set_all_tc_msdu(np);
}

static int netc_setup(struct dsa_switch *ds)
{
	struct netc_switch *priv = ds->priv;
	struct dsa_port *dp;
	int err;

	err = netc_init_switch_id(priv);
	if (err)
		return err;

	err = netc_init_all_ports(priv);
	if (err)
		return err;

	err = netc_init_ntmp_user(priv);
	if (err)
		return err;

	netc_switch_fixed_config(priv);

	/* default setting for ports */
	dsa_switch_for_each_available_port(dp, ds)
		netc_port_default_config(priv->ports[dp->index]);

	return 0;
}

static void netc_teardown(struct dsa_switch *ds)
{
	struct netc_switch *priv = ds->priv;

	netc_free_ntmp_user(priv);
}

static bool netc_port_is_emdio_consumer(struct device_node *node)
{
	struct device_node *mdio_node;

	/* If the port node has phy-handle property and it does
	 * not contain a mdio child node, then the port is the
	 * EMDIO consumer.
	 */
	mdio_node = of_get_child_by_name(node, "mdio");
	if (!mdio_node)
		return true;

	of_node_put(mdio_node);

	return false;
}

/* Currently, phylink_of_phy_connect() is called by dsa_user_create(),
 * so if the switch uses the external MDIO controller (like the EMDIO
 * function) to manage the external PHYs. The MDIO bus may not be
 * created when phylink_of_phy_connect() is called, so it will return
 * an error and cause the switch driver to fail to probe.
 * This workaround can be removed when DSA phylink_of_phy_connect()
 * calls are moved from probe() to ndo_open().
 */
static int netc_switch_check_emdio_is_ready(struct device *dev)
{
	struct device_node *ports, *phy_node;
	struct phy_device *phydev;
	int err = 0;

	ports = of_get_child_by_name(dev->of_node, "ethernet-ports");
	if (!ports) {
		dev_err(dev, "Cannot find the ethernet-ports node\n");
		return -EINVAL;
	}

	for_each_available_child_of_node_scoped(ports, child) {
		/* If the node does not have phy-handle property, then the
		 * port does not connect to a PHY, so the port is not the
		 * EMDIO consumer.
		 */
		phy_node = of_parse_phandle(child, "phy-handle", 0);
		if (!phy_node)
			continue;

		/* Note that from the hardware perspective, the switch ports
		 * do not support sharing the MDIO bus defined under one port.
		 * Each port can only access its own external PHY through its
		 * port MDIO bus.
		 */
		if (!netc_port_is_emdio_consumer(child)) {
			of_node_put(phy_node);
			continue;
		}

		phydev = of_phy_find_device(phy_node);
		of_node_put(phy_node);
		if (!phydev) {
			err = -EPROBE_DEFER;
			goto out;
		}

		put_device(&phydev->mdio.dev);
	}

out:
	of_node_put(ports);

	return err;
}

static int netc_switch_pci_init(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct netc_switch_regs *regs;
	struct netc_switch *priv;
	void __iomem *base;
	int err;

	pcie_flr(pdev);
	err = pcim_enable_device(pdev);
	if (err)
		return dev_err_probe(dev, err, "Failed to enable device\n");

	err = pcim_request_all_regions(pdev, KBUILD_MODNAME);
	if (err)
		return dev_err_probe(dev, err, "Failed to request regions\n");

	/* The command BD rings and NTMP tables need DMA. No need to check
	 * the return value, because it never returns fail when the mask is
	 * DMA_BIT_MASK(64), see dma-api-howto.rst.
	 */
	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	if (pci_resource_len(pdev, NETC_REGS_BAR) < NETC_REGS_SIZE) {
		return dev_err_probe(dev, -EINVAL,
				     "Invalid register space size\n");
	}

	base = pcim_iomap(pdev, NETC_REGS_BAR, 0);
	if (!base)
		return dev_err_probe(dev, -ENXIO, "pcim_iomap() failed\n");

	pci_set_master(pdev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	priv->dev = dev;

	regs = &priv->regs;
	regs->base = base;
	regs->port = regs->base + NETC_REGS_PORT_BASE;
	regs->global = regs->base + NETC_REGS_GLOBAL_BASE;
	pci_set_drvdata(pdev, priv);

	return 0;
}

static void netc_switch_get_ip_revision(struct netc_switch *priv)
{
	struct netc_switch_regs *regs = &priv->regs;
	u32 val = netc_glb_rd(regs, NETC_IPBRR0);

	priv->revision = FIELD_GET(IPBRR0_IP_REV, val);
}

static void netc_phylink_get_caps(struct dsa_switch *ds, int port,
				  struct phylink_config *config)
{
	struct netc_switch *priv = ds->priv;

	priv->info->phylink_get_caps(port, config);
}

static void netc_port_set_mac_mode(struct netc_port *np,
				   unsigned int mode,
				   phy_interface_t phy_mode)
{
	u32 mask = PM_IF_MODE_IFMODE | PM_IF_MODE_REVMII;
	u32 val = 0;

	switch (phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val |= IFMODE_RGMII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val |= IFMODE_RMII;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		val |= PM_IF_MODE_REVMII;
		fallthrough;
	case PHY_INTERFACE_MODE_MII:
		val |= IFMODE_MII;
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		val |= IFMODE_SGMII;
		break;
	default:
		break;
	}

	netc_mac_port_rmw(np, NETC_PM_IF_MODE(0), mask, val);
}

static void netc_mac_config(struct phylink_config *config, unsigned int mode,
			    const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);

	netc_port_set_mac_mode(NETC_PORT(dp->ds, dp->index), mode,
			       state->interface);
}

static void netc_port_set_speed(struct netc_port *np, int speed)
{
	netc_port_rmw(np, NETC_PCR, PCR_PSPEED, PSPEED_SET_VAL(speed));
}

static void netc_port_set_rgmii_mac(struct netc_port *np,
				    int speed, int duplex)
{
	u32 mask, val;

	mask = PM_IF_MODE_SSP | PM_IF_MODE_HD | PM_IF_MODE_M10;

	switch (speed) {
	default:
	case SPEED_1000:
		val = FIELD_PREP(PM_IF_MODE_SSP, SSP_1G);
		break;
	case SPEED_100:
		val = FIELD_PREP(PM_IF_MODE_SSP, SSP_100M);
		break;
	case SPEED_10:
		val = FIELD_PREP(PM_IF_MODE_SSP, SSP_10M);
		break;
	}

	if (duplex != DUPLEX_FULL)
		val |= PM_IF_MODE_HD;

	netc_mac_port_rmw(np, NETC_PM_IF_MODE(0), mask, val);
}

static void netc_port_set_rmii_mii_mac(struct netc_port *np,
				       int speed, int duplex)
{
	u32 mask, val = 0;

	mask = PM_IF_MODE_SSP | PM_IF_MODE_HD | PM_IF_MODE_M10;

	if (speed == SPEED_10)
		val |= PM_IF_MODE_M10;

	if (duplex != DUPLEX_FULL)
		val |= PM_IF_MODE_HD;

	netc_mac_port_rmw(np, NETC_PM_IF_MODE(0), mask, val);
}

static void netc_port_mac_rx_enable(struct netc_port *np)
{
	netc_port_rmw(np, NETC_POR, POR_RXDIS, 0);
	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_RX_EN,
			  PM_CMD_CFG_RX_EN);
}

static void netc_port_wait_rx_empty(struct netc_port *np, int mac)
{
	u32 val;

	/* PM_IEVENT_RX_EMPTY is a read-only bit, it is automatically set by
	 * hardware if RX FIFO is empty and no RX packet receive in process.
	 * And it is automatically cleared if RX FIFO is not empty or RX
	 * packet receive in process.
	 */
	if (read_poll_timeout(netc_port_rd, val, val & PM_IEVENT_RX_EMPTY,
			      100, 10000, false, np, NETC_PM_IEVENT(mac)))
		dev_warn(np->switch_priv->dev,
			 "swp%d MAC%d: RX is not idle\n", np->dp->index, mac);
}

static void netc_port_mac_rx_graceful_stop(struct netc_port *np)
{
	u32 val;

	if (is_netc_pseudo_port(np))
		goto rx_disable;

	if (np->caps.pmac) {
		netc_port_rmw(np, NETC_PM_CMD_CFG(1), PM_CMD_CFG_RX_EN, 0);
		netc_port_wait_rx_empty(np, 1);
	}

	netc_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_RX_EN, 0);
	netc_port_wait_rx_empty(np, 0);

	if (read_poll_timeout(netc_port_rd, val, !(val & PSR_RX_BUSY),
			      100, 10000, false, np, NETC_PSR))
		dev_warn(np->switch_priv->dev, "swp%d RX is busy\n",
			 np->dp->index);

rx_disable:
	netc_port_rmw(np, NETC_POR, POR_RXDIS, POR_RXDIS);
}

static void netc_port_mac_tx_enable(struct netc_port *np)
{
	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_TX_EN,
			  PM_CMD_CFG_TX_EN);
	netc_port_rmw(np, NETC_POR, POR_TXDIS, 0);
}

static void netc_port_wait_tx_empty(struct netc_port *np, int mac)
{
	u32 val;

	/* PM_IEVENT_TX_EMPTY is a read-only bit, it is automatically set by
	 * hardware if TX FIFO is empty. And it is automatically cleared if
	 * TX FIFO is not empty.
	 */
	if (read_poll_timeout(netc_port_rd, val, val & PM_IEVENT_TX_EMPTY,
			      100, 10000, false, np, NETC_PM_IEVENT(mac)))
		dev_warn(np->switch_priv->dev,
			 "swp%d MAC%d: TX FIFO is not empty\n",
			 np->dp->index, mac);
}

static void netc_port_mac_tx_graceful_stop(struct netc_port *np)
{
	netc_port_rmw(np, NETC_POR, POR_TXDIS, POR_TXDIS);

	if (is_netc_pseudo_port(np))
		return;

	netc_port_wait_tx_empty(np, 0);
	if (np->caps.pmac)
		netc_port_wait_tx_empty(np, 1);

	netc_mac_port_rmw(np, NETC_PM_CMD_CFG(0), PM_CMD_CFG_TX_EN, 0);
}

static void netc_mac_link_up(struct phylink_config *config,
			     struct phy_device *phy, unsigned int mode,
			     phy_interface_t interface, int speed,
			     int duplex, bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct netc_port *np;

	np = NETC_PORT(dp->ds, dp->index);
	netc_port_set_speed(np, speed);

	if (phy_interface_mode_is_rgmii(interface))
		netc_port_set_rgmii_mac(np, speed, duplex);

	if (interface == PHY_INTERFACE_MODE_RMII ||
	    interface == PHY_INTERFACE_MODE_REVMII ||
	    interface == PHY_INTERFACE_MODE_MII)
		netc_port_set_rmii_mii_mac(np, speed, duplex);

	netc_port_mac_tx_enable(np);
	netc_port_mac_rx_enable(np);
}

static void netc_mac_link_down(struct phylink_config *config,
			       unsigned int mode,
			       phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct netc_port *np;

	np = NETC_PORT(dp->ds, dp->index);
	netc_port_mac_rx_graceful_stop(np);
	netc_port_mac_tx_graceful_stop(np);
}

static const struct phylink_mac_ops netc_phylink_mac_ops = {
	.mac_config		= netc_mac_config,
	.mac_link_up		= netc_mac_link_up,
	.mac_link_down		= netc_mac_link_down,
};

static const struct dsa_switch_ops netc_switch_ops = {
	.get_tag_protocol		= netc_get_tag_protocol,
	.setup				= netc_setup,
	.teardown			= netc_teardown,
	.phylink_get_caps		= netc_phylink_get_caps,
};

static int netc_switch_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct device_node *node = dev_of_node(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct netc_switch *priv;
	struct dsa_switch *ds;
	int err;

	if (!node)
		return dev_err_probe(dev, -ENODEV,
				     "No DT bindings, skipping\n");

	err = netc_switch_check_emdio_is_ready(dev);
	if (err)
		return err;

	err = netc_switch_pci_init(pdev);
	if (err)
		return err;

	priv = pci_get_drvdata(pdev);
	netc_switch_get_ip_revision(priv);

	err = netc_switch_platform_probe(priv);
	if (err)
		return err;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->num_ports = priv->info->num_ports;
	ds->num_tx_queues = NETC_TC_NUM;
	ds->ops = &netc_switch_ops;
	ds->phylink_mac_ops = &netc_phylink_mac_ops;
	ds->priv = priv;
	priv->ds = ds;

	err = dsa_register_switch(ds);
	if (err)
		return dev_err_probe(dev, err,
				     "Failed to register DSA switch\n");

	return 0;
}

static void netc_switch_remove(struct pci_dev *pdev)
{
	struct netc_switch *priv = pci_get_drvdata(pdev);

	if (!priv)
		return;

	dsa_unregister_switch(priv->ds);
}

static void netc_switch_shutdown(struct pci_dev *pdev)
{
	struct netc_switch *priv = pci_get_drvdata(pdev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);
	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id netc_switch_ids[] = {
	{ PCI_DEVICE(NETC_SWITCH_VENDOR_ID, NETC_SWITCH_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, netc_switch_ids);

static struct pci_driver netc_switch_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= netc_switch_ids,
	.probe		= netc_switch_probe,
	.remove		= netc_switch_remove,
	.shutdown	= netc_switch_shutdown,
};
module_pci_driver(netc_switch_driver);

MODULE_DESCRIPTION("NXP NETC Switch driver");
MODULE_LICENSE("Dual BSD/GPL");
