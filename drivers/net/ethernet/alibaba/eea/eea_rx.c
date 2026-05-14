// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#include <net/netdev_rx_queue.h>
#include <net/page_pool/helpers.h>

#include "eea_adminq.h"
#include "eea_net.h"
#include "eea_pci.h"
#include "eea_ring.h"

#define EEA_ENABLE_F_NAPI        BIT(0)

#define EEA_PAGE_FRAGS_NUM 1024

static void eea_free_rx_buffer(struct eea_net_rx *rx, struct eea_rx_meta *meta,
			       bool allow_direct)
{
	u32 drain_count;

	drain_count = EEA_PAGE_FRAGS_NUM - meta->frags;

	if (page_pool_unref_page(meta->page, drain_count) == 0)
		page_pool_put_unrefed_page(rx->pp, meta->page, -1,
					   allow_direct);

	meta->page = NULL;
}

static void eea_free_rx_hdr(struct eea_net_rx *rx, struct eea_net_cfg *cfg)
{
	struct eea_rx_meta *meta;
	int i;

	for (i = 0; i < cfg->rx_ring_depth; ++i) {
		meta = &rx->meta[i];
		meta->hdr_addr = NULL;

		if (!meta->hdr_page)
			continue;

		dma_unmap_page(rx->dma_dev, meta->hdr_dma, PAGE_SIZE,
			       DMA_FROM_DEVICE);
		put_page(meta->hdr_page);

		meta->hdr_page = NULL;
	}
}

static int eea_alloc_rx_hdr(struct eea_net_init_ctx *ctx, struct eea_net_rx *rx)
{
	struct page *hdr_page = NULL;
	struct eea_rx_meta *meta;
	u32 offset = 0, hdrsize;
	struct device *dmadev;
	dma_addr_t dma;
	int i;

	dmadev = ctx->edev->dma_dev;
	hdrsize = ctx->cfg.split_hdr;

	for (i = 0; i < ctx->cfg.rx_ring_depth; ++i) {
		meta = &rx->meta[i];
		meta->hdr_page = NULL;

		if (!hdr_page || offset + hdrsize > PAGE_SIZE) {
			hdr_page = alloc_page(GFP_KERNEL);
			if (!hdr_page)
				goto err;

			dma = dma_map_page(dmadev, hdr_page, 0, PAGE_SIZE,
					   DMA_FROM_DEVICE);

			if (unlikely(dma_mapping_error(dmadev, dma))) {
				put_page(hdr_page);
				goto err;
			}

			offset = 0;
			meta->hdr_page = hdr_page;
		}

		meta->hdr_dma = dma + offset;
		meta->hdr_addr = page_address(hdr_page) + offset;
		offset += hdrsize;
	}

	return 0;

err:
	eea_free_rx_hdr(rx, &ctx->cfg);
	return -ENOMEM;
}

static int eea_poll(struct napi_struct *napi, int budget)
{
	/* Empty function; will be implemented in a subsequent commit. */
	return 0;
}

static void eea_free_rx_buffers(struct eea_net_rx *rx, struct eea_net_cfg *cfg)
{
	struct eea_rx_meta *meta;
	u32 i;

	for (i = 0; i < cfg->rx_ring_depth; ++i) {
		meta = &rx->meta[i];
		if (!meta->page)
			continue;

		eea_free_rx_buffer(rx, meta, false);
	}
}

static struct page_pool *eea_create_pp(struct eea_net_init_ctx *ctx, u32 idx)
{
	struct page_pool_params pp_params = {0};

	pp_params.order     = 0;
	pp_params.flags     = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV;
	pp_params.pool_size = ctx->cfg.rx_ring_depth;
	pp_params.nid       = dev_to_node(ctx->edev->dma_dev);
	pp_params.dev       = ctx->edev->dma_dev;
	pp_params.netdev    = ctx->netdev;
	pp_params.dma_dir   = DMA_FROM_DEVICE;
	pp_params.max_len   = PAGE_SIZE;
	pp_params.queue_idx = idx;

	return page_pool_create(&pp_params);
}

static void eea_destroy_page_pool(struct eea_net_rx *rx)
{
	if (rx->pp)
		page_pool_destroy(rx->pp);
}

void enet_rx_stop(struct eea_net_rx *rx)
{
	if (rx->flags & EEA_ENABLE_F_NAPI) {
		rx->flags &= ~EEA_ENABLE_F_NAPI;

		disable_irq(rx->enet->irq_blks[rx->index].irq);
		napi_disable(rx->napi);

		page_pool_disable_direct_recycling(rx->pp);
		netif_napi_del(rx->napi);
	}
}

void enet_rx_start(struct eea_net_rx *rx)
{
	netif_napi_add(rx->enet->netdev, rx->napi, eea_poll);

	page_pool_enable_direct_recycling(rx->pp, rx->napi);

	napi_enable(rx->napi);

	rx->flags |= EEA_ENABLE_F_NAPI;

	local_bh_disable();
	napi_schedule(rx->napi);
	local_bh_enable();

	enable_irq(rx->enet->irq_blks[rx->index].irq);
}

/* Maybe called before eea_bind_q_and_cfg. So the cfg must be passed. */
void eea_free_rx(struct eea_net_rx *rx, struct eea_net_cfg *cfg)
{
	if (!rx)
		return;

	if (rx->ering) {
		eea_ering_free(rx->ering);
		rx->ering = NULL;
	}

	if (rx->meta) {
		eea_free_rx_buffers(rx, cfg);
		eea_free_rx_hdr(rx, cfg);
		kvfree(rx->meta);
		rx->meta = NULL;
	}

	if (rx->pp) {
		eea_destroy_page_pool(rx);
		rx->pp = NULL;
	}

	kfree(rx);
}

static void eea_rx_meta_init(struct eea_net_rx *rx, u32 num)
{
	struct eea_rx_meta *meta;
	int i;

	rx->free = NULL;

	for (i = 0; i < num; ++i) {
		meta = &rx->meta[i];
		meta->id = i;
		meta->next = rx->free;
		rx->free = meta;
	}
}

struct eea_net_rx *eea_alloc_rx(struct eea_net_init_ctx *ctx, u32 idx)
{
	struct eea_ring *ering;
	struct eea_net_rx *rx;
	int err;

	rx = kzalloc(sizeof(*rx), GFP_KERNEL);
	if (!rx)
		return rx;

	rx->index = idx;
	snprintf(rx->name, sizeof(rx->name), "rx.%u", idx);

	/* ering */
	ering = eea_ering_alloc(idx * 2, ctx->cfg.rx_ring_depth, ctx->edev,
				ctx->cfg.rx_sq_desc_size,
				ctx->cfg.rx_cq_desc_size,
				rx->name);
	if (!ering)
		goto err_free_rx;

	rx->ering = ering;

	rx->dma_dev = ctx->edev->dma_dev;

	/* meta */
	rx->meta = kvcalloc(ctx->cfg.rx_ring_depth,
			    sizeof(*rx->meta), GFP_KERNEL);
	if (!rx->meta)
		goto err_free_rx;

	eea_rx_meta_init(rx, ctx->cfg.rx_ring_depth);

	if (ctx->cfg.split_hdr) {
		err = eea_alloc_rx_hdr(ctx, rx);
		if (err)
			goto err_free_rx;
	}

	rx->pp = eea_create_pp(ctx, idx);
	if (IS_ERR(rx->pp)) {
		err = PTR_ERR(rx->pp);
		rx->pp = NULL;
		goto err_free_rx;
	}

	return rx;

err_free_rx:
	eea_free_rx(rx, &ctx->cfg);
	return NULL;
}
