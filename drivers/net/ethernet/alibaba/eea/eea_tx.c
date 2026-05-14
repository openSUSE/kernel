// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <net/netdev_queues.h>

#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

struct eea_tx_meta {
	struct eea_tx_meta *next;

	u32 id;

	union {
		struct sk_buff *skb;
		void *data;
	};

	u32 num;

	dma_addr_t dma_addr;
	struct eea_tx_desc *desc;
	u32 dma_len;
};

int eea_poll_tx(struct eea_net_tx *tx, int budget)
{
	/* Empty function; will be implemented in a subsequent commit. */
	return budget;
}

netdev_tx_t eea_tx_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	/* Empty function; will be implemented in a subsequent commit. */
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void eea_free_meta(struct eea_net_tx *tx, struct eea_net_cfg *cfg)
{
	kvfree(tx->meta);
	tx->meta = NULL;
}

/* Maybe called before eea_bind_q_and_cfg. So the cfg must be passed. */
void eea_free_tx(struct eea_net_tx *tx, struct eea_net_cfg *cfg)
{
	if (!tx)
		return;

	if (tx->ering) {
		eea_ering_free(tx->ering);
		tx->ering = NULL;
	}

	if (tx->meta)
		eea_free_meta(tx, cfg);
}

int eea_alloc_tx(struct eea_net_init_ctx *ctx, struct eea_net_tx *tx, u32 idx)
{
	struct eea_tx_meta *meta;
	struct eea_ring *ering;
	u32 i;

	snprintf(tx->name, sizeof(tx->name), "tx.%u", idx);

	ering = eea_ering_alloc(idx * 2 + 1, ctx->cfg.tx_ring_depth, ctx->edev,
				ctx->cfg.tx_sq_desc_size,
				ctx->cfg.tx_cq_desc_size,
				tx->name);
	if (!ering)
		goto err_free_tx;

	tx->ering = ering;
	tx->index = idx;
	tx->dma_dev = ctx->edev->dma_dev;

	/* meta */
	tx->meta = kvcalloc(ctx->cfg.tx_ring_depth,
			    sizeof(*tx->meta), GFP_KERNEL);
	if (!tx->meta)
		goto err_free_tx;

	for (i = 0; i < ctx->cfg.tx_ring_depth; ++i) {
		meta = &tx->meta[i];
		meta->id = i;
		meta->next = tx->free;
		tx->free = meta;
	}

	return 0;

err_free_tx:
	eea_free_tx(tx, &ctx->cfg);
	return -ENOMEM;
}
