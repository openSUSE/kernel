/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2025-2026 NXP
 */

#ifndef _NETC_SWITCH_H
#define _NETC_SWITCH_H

#include <linux/dsa/tag_netc.h>
#include <linux/fsl/netc_global.h>
#include <linux/fsl/ntmp.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/pci.h>

#include "netc_switch_hw.h"

#define NETC_REGS_BAR			0
#define NETC_REGS_SIZE			0x80000
#define NETC_MSIX_TBL_BAR		2
#define NETC_REGS_PORT_BASE		0x4000
/* register block size per port  */
#define NETC_REGS_PORT_SIZE		0x4000
#define PORT_IOBASE(p)			(NETC_REGS_PORT_SIZE * (p))
#define NETC_REGS_GLOBAL_BASE		0x70000

#define NETC_SWITCH_REV_4_3		0x0403

#define NETC_TC_NUM			8
#define NETC_CBDR_NUM			2

#define NETC_MAX_FRAME_LEN		9600

#define NETC_STANDALONE_PVID		0

struct netc_switch;

struct netc_switch_info {
	u32 num_ports;
	void (*phylink_get_caps)(int port, struct phylink_config *config);
};

struct netc_port_caps {
	u32 half_duplex:1; /* indicates whether the port support half-duplex */
	u32 pmac:1;	  /* indicates whether the port has preemption MAC */
	u32 pseudo_link:1;
};

enum netc_host_reason {
	/* Software defined host reasons */
	NETC_HR_HOST_FLOOD = 8,
};

struct netc_port {
	void __iomem *iobase;
	struct netc_switch *switch_priv;
	struct netc_port_caps caps;
	struct dsa_port *dp;
	struct clk *ref_clk; /* RGMII/RMII reference clock */
	struct mii_bus *emdio;

	u16 enable:1;
	u16 uc:1;
	u16 mc:1;
	struct ipft_entry_data *host_flood;
};

struct netc_switch_regs {
	void __iomem *base;
	void __iomem *port;
	void __iomem *global;
};

struct netc_fdb_entry {
	u32 entry_id;
	struct fdbt_cfge_data cfge;
	struct fdbt_keye_data keye;
	struct hlist_node node;
};

struct netc_switch {
	struct pci_dev *pdev;
	struct device *dev;
	struct dsa_switch *ds;
	u16 revision;

	const struct netc_switch_info *info;
	struct netc_switch_regs regs;
	struct netc_port **ports;

	struct ntmp_user ntmp;
	struct hlist_head fdb_list;
	struct mutex fdbt_lock; /* FDB table lock */

	/* Switch hardware capabilities */
	u32 htmcapr_num_words;
};

#define NETC_PRIV(ds)			((struct netc_switch *)((ds)->priv))
#define NETC_PORT(ds, port_id)		(NETC_PRIV(ds)->ports[(port_id)])

/* Write/Read Switch base registers */
#define netc_base_rd(r, o)		netc_read((r)->base + (o))
#define netc_base_wr(r, o, v)		netc_write((r)->base + (o), v)

/* Write/Read registers of Switch Port (including pseudo MAC port) */
#define netc_port_rd(p, o)		netc_read((p)->iobase + (o))
#define netc_port_wr(p, o, v)		netc_write((p)->iobase + (o), v)

/* Write/Read Switch global registers */
#define netc_glb_rd(r, o)		netc_read((r)->global + (o))
#define netc_glb_wr(r, o, v)		netc_write((r)->global + (o), v)

static inline bool is_netc_pseudo_port(struct netc_port *np)
{
	return np->caps.pseudo_link;
}

static inline void netc_add_fdb_entry(struct netc_switch *priv,
				      struct netc_fdb_entry *entry)
{
	hlist_add_head(&entry->node, &priv->fdb_list);
}

static inline void netc_del_fdb_entry(struct netc_fdb_entry *entry)
{
	hlist_del(&entry->node);
	kfree(entry);
}

int netc_switch_platform_probe(struct netc_switch *priv);

#endif
