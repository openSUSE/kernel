// SPDX-License-Identifier: GPL-2.0-only
/* net/core/xdp.c
 *
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/rhashtable.h>
#include <linux/bug.h>
#include <net/page_pool/helpers.h>

#include <net/hotdata.h>
#include <net/netdev_lock.h>
#include <net/xdp.h>
#include <net/xdp_priv.h> /* struct xdp_mem_allocator */
#include <trace/events/xdp.h>
#include <net/xdp_sock_drv.h>

#define REG_STATE_NEW		0x0
#define REG_STATE_REGISTERED	0x1
#define REG_STATE_UNREGISTERED	0x2
#define REG_STATE_UNUSED	0x3

static DEFINE_IDA(mem_id_pool);
static DEFINE_MUTEX(mem_id_lock);
#define MEM_ID_MAX 0xFFFE
#define MEM_ID_MIN 1
static int mem_id_next = MEM_ID_MIN;

static bool mem_id_init; /* false */
static struct rhashtable *mem_id_ht;

static u32 xdp_mem_id_hashfn(const void *data, u32 len, u32 seed)
{
	const u32 *k = data;
	const u32 key = *k;

	BUILD_BUG_ON(sizeof_field(struct xdp_mem_allocator, mem.id)
		     != sizeof(u32));

	/* Use cyclic increasing ID as direct hash key */
	return key;
}

static int xdp_mem_id_cmp(struct rhashtable_compare_arg *arg,
			  const void *ptr)
{
	const struct xdp_mem_allocator *xa = ptr;
	u32 mem_id = *(u32 *)arg->key;

	return xa->mem.id != mem_id;
}

static const struct rhashtable_params mem_id_rht_params = {
	.nelem_hint = 64,
	.head_offset = offsetof(struct xdp_mem_allocator, node),
	.key_offset  = offsetof(struct xdp_mem_allocator, mem.id),
	.key_len = sizeof_field(struct xdp_mem_allocator, mem.id),
	.max_size = MEM_ID_MAX,
	.min_size = 8,
	.automatic_shrinking = true,
	.hashfn    = xdp_mem_id_hashfn,
	.obj_cmpfn = xdp_mem_id_cmp,
};

static void __xdp_mem_allocator_rcu_free(struct rcu_head *rcu)
{
	struct xdp_mem_allocator *xa;

	xa = container_of(rcu, struct xdp_mem_allocator, rcu);

	/* Allow this ID to be reused */
	ida_free(&mem_id_pool, xa->mem.id);

	kfree(xa);
}

static void mem_xa_remove(struct xdp_mem_allocator *xa)
{
	trace_mem_disconnect(xa);

	if (!rhashtable_remove_fast(mem_id_ht, &xa->node, mem_id_rht_params))
		call_rcu(&xa->rcu, __xdp_mem_allocator_rcu_free);
}

static void mem_allocator_disconnect(void *allocator)
{
	struct xdp_mem_allocator *xa;
	struct rhashtable_iter iter;

	mutex_lock(&mem_id_lock);

	rhashtable_walk_enter(mem_id_ht, &iter);
	do {
		rhashtable_walk_start(&iter);

		while ((xa = rhashtable_walk_next(&iter)) && !IS_ERR(xa)) {
			if (xa->allocator == allocator)
				mem_xa_remove(xa);
		}

		rhashtable_walk_stop(&iter);

	} while (xa == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&iter);

	mutex_unlock(&mem_id_lock);
}

void xdp_unreg_mem_model(struct xdp_mem_info *mem)
{
	struct xdp_mem_allocator *xa;
	int type = mem->type;
	int id = mem->id;

	/* Reset mem info to defaults */
	mem->id = 0;
	mem->type = 0;

	if (id == 0)
		return;

	if (type == MEM_TYPE_PAGE_POOL) {
		xa = rhashtable_lookup_fast(mem_id_ht, &id, mem_id_rht_params);
		page_pool_destroy(xa->page_pool);
	}
}
EXPORT_SYMBOL_GPL(xdp_unreg_mem_model);

void xdp_rxq_info_unreg_mem_model(struct xdp_rxq_info *xdp_rxq)
{
	if (xdp_rxq->reg_state != REG_STATE_REGISTERED) {
		WARN(1, "Missing register, driver bug");
		return;
	}

	xdp_unreg_mem_model(&xdp_rxq->mem);
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_unreg_mem_model);

void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq)
{
	/* Simplify driver cleanup code paths, allow unreg "unused" */
	if (xdp_rxq->reg_state == REG_STATE_UNUSED)
		return;

	xdp_rxq_info_unreg_mem_model(xdp_rxq);

	xdp_rxq->reg_state = REG_STATE_UNREGISTERED;
	xdp_rxq->dev = NULL;
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_unreg);

static void xdp_rxq_info_init(struct xdp_rxq_info *xdp_rxq)
{
	memset(xdp_rxq, 0, sizeof(*xdp_rxq));
}

/* Returns 0 on success, negative on failure */
int __xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq,
		       struct net_device *dev, u32 queue_index,
		       unsigned int napi_id, u32 frag_size)
{
	if (!dev) {
		WARN(1, "Missing net_device from driver");
		return -ENODEV;
	}

	if (xdp_rxq->reg_state == REG_STATE_UNUSED) {
		WARN(1, "Driver promised not to register this");
		return -EINVAL;
	}

	if (xdp_rxq->reg_state == REG_STATE_REGISTERED) {
		WARN(1, "Missing unregister, handled but fix driver");
		xdp_rxq_info_unreg(xdp_rxq);
	}

	/* State either UNREGISTERED or NEW */
	xdp_rxq_info_init(xdp_rxq);
	xdp_rxq->dev = dev;
	xdp_rxq->queue_index = queue_index;
	xdp_rxq->frag_size = frag_size;

	xdp_rxq->reg_state = REG_STATE_REGISTERED;
	return 0;
}
EXPORT_SYMBOL_GPL(__xdp_rxq_info_reg);

void xdp_rxq_info_unused(struct xdp_rxq_info *xdp_rxq)
{
	xdp_rxq->reg_state = REG_STATE_UNUSED;
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_unused);

bool xdp_rxq_info_is_reg(struct xdp_rxq_info *xdp_rxq)
{
	return (xdp_rxq->reg_state == REG_STATE_REGISTERED);
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_is_reg);

static int __mem_id_init_hash_table(void)
{
	struct rhashtable *rht;
	int ret;

	if (unlikely(mem_id_init))
		return 0;

	rht = kzalloc(sizeof(*rht), GFP_KERNEL);
	if (!rht)
		return -ENOMEM;

	ret = rhashtable_init(rht, &mem_id_rht_params);
	if (ret < 0) {
		kfree(rht);
		return ret;
	}
	mem_id_ht = rht;
	smp_mb(); /* mutex lock should provide enough pairing */
	mem_id_init = true;

	return 0;
}

/* Allocate a cyclic ID that maps to allocator pointer.
 * See: https://www.kernel.org/doc/html/latest/core-api/idr.html
 *
 * Caller must lock mem_id_lock.
 */
static int __mem_id_cyclic_get(gfp_t gfp)
{
	int retries = 1;
	int id;

again:
	id = ida_alloc_range(&mem_id_pool, mem_id_next, MEM_ID_MAX - 1, gfp);
	if (id < 0) {
		if (id == -ENOSPC) {
			/* Cyclic allocator, reset next id */
			if (retries--) {
				mem_id_next = MEM_ID_MIN;
				goto again;
			}
		}
		return id; /* errno */
	}
	mem_id_next = id + 1;

	return id;
}

static bool __is_supported_mem_type(enum xdp_mem_type type)
{
	if (type == MEM_TYPE_PAGE_POOL)
		return is_page_pool_compiled_in();

	if (type >= MEM_TYPE_MAX)
		return false;

	return true;
}

static struct xdp_mem_allocator *__xdp_reg_mem_model(struct xdp_mem_info *mem,
						     enum xdp_mem_type type,
						     void *allocator)
{
	struct xdp_mem_allocator *xdp_alloc;
	gfp_t gfp = GFP_KERNEL;
	int id, errno, ret;
	void *ptr;

	if (!__is_supported_mem_type(type))
		return ERR_PTR(-EOPNOTSUPP);

	mem->type = type;

	if (!allocator) {
		if (type == MEM_TYPE_PAGE_POOL)
			return ERR_PTR(-EINVAL); /* Setup time check page_pool req */
		return NULL;
	}

	/* Delay init of rhashtable to save memory if feature isn't used */
	if (!mem_id_init) {
		mutex_lock(&mem_id_lock);
		ret = __mem_id_init_hash_table();
		mutex_unlock(&mem_id_lock);
		if (ret < 0)
			return ERR_PTR(ret);
	}

	xdp_alloc = kzalloc(sizeof(*xdp_alloc), gfp);
	if (!xdp_alloc)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&mem_id_lock);
	id = __mem_id_cyclic_get(gfp);
	if (id < 0) {
		errno = id;
		goto err;
	}
	mem->id = id;
	xdp_alloc->mem = *mem;
	xdp_alloc->allocator = allocator;

	/* Insert allocator into ID lookup table */
	ptr = rhashtable_insert_slow(mem_id_ht, &id, &xdp_alloc->node);
	if (IS_ERR(ptr)) {
		ida_free(&mem_id_pool, mem->id);
		mem->id = 0;
		errno = PTR_ERR(ptr);
		goto err;
	}

	if (type == MEM_TYPE_PAGE_POOL)
		page_pool_use_xdp_mem(allocator, mem_allocator_disconnect, mem);

	mutex_unlock(&mem_id_lock);

	return xdp_alloc;
err:
	mutex_unlock(&mem_id_lock);
	kfree(xdp_alloc);
	return ERR_PTR(errno);
}

int xdp_reg_mem_model(struct xdp_mem_info *mem,
		      enum xdp_mem_type type, void *allocator)
{
	struct xdp_mem_allocator *xdp_alloc;

	xdp_alloc = __xdp_reg_mem_model(mem, type, allocator);
	if (IS_ERR(xdp_alloc))
		return PTR_ERR(xdp_alloc);
	return 0;
}
EXPORT_SYMBOL_GPL(xdp_reg_mem_model);

int xdp_rxq_info_reg_mem_model(struct xdp_rxq_info *xdp_rxq,
			       enum xdp_mem_type type, void *allocator)
{
	struct xdp_mem_allocator *xdp_alloc;

	if (xdp_rxq->reg_state != REG_STATE_REGISTERED) {
		WARN(1, "Missing register, driver bug");
		return -EFAULT;
	}

	xdp_alloc = __xdp_reg_mem_model(&xdp_rxq->mem, type, allocator);
	if (IS_ERR(xdp_alloc))
		return PTR_ERR(xdp_alloc);

	if (type == MEM_TYPE_XSK_BUFF_POOL && allocator)
		xsk_pool_set_rxq_info(allocator, xdp_rxq);

	if (trace_mem_connect_enabled() && xdp_alloc)
		trace_mem_connect(xdp_alloc, xdp_rxq);
	return 0;
}

EXPORT_SYMBOL_GPL(xdp_rxq_info_reg_mem_model);

/**
 * xdp_reg_page_pool - register &page_pool as a memory provider for XDP
 * @pool: &page_pool to register
 *
 * Can be used to register pools manually without connecting to any XDP RxQ
 * info, so that the XDP layer will be aware of them. Then, they can be
 * attached to an RxQ info manually via xdp_rxq_info_attach_page_pool().
 *
 * Return: %0 on success, -errno on error.
 */
int xdp_reg_page_pool(struct page_pool *pool)
{
	struct xdp_mem_info mem;

	return xdp_reg_mem_model(&mem, MEM_TYPE_PAGE_POOL, pool);
}
EXPORT_SYMBOL_GPL(xdp_reg_page_pool);

/**
 * xdp_unreg_page_pool - unregister &page_pool from the memory providers list
 * @pool: &page_pool to unregister
 *
 * A shorthand for manual unregistering page pools. If the pool was previously
 * attached to an RxQ info, it must be detached first.
 */
void xdp_unreg_page_pool(const struct page_pool *pool)
{
	struct xdp_mem_info mem = {
		.type	= MEM_TYPE_PAGE_POOL,
		.id	= pool->xdp_mem_id,
	};

	xdp_unreg_mem_model(&mem);
}
EXPORT_SYMBOL_GPL(xdp_unreg_page_pool);

/**
 * xdp_rxq_info_attach_page_pool - attach registered pool to RxQ info
 * @xdp_rxq: XDP RxQ info to attach the pool to
 * @pool: pool to attach
 *
 * If the pool was registered manually, this function must be called instead
 * of xdp_rxq_info_reg_mem_model() to connect it to the RxQ info.
 */
void xdp_rxq_info_attach_page_pool(struct xdp_rxq_info *xdp_rxq,
				   const struct page_pool *pool)
{
	struct xdp_mem_info mem = {
		.type	= MEM_TYPE_PAGE_POOL,
		.id	= pool->xdp_mem_id,
	};

	xdp_rxq_info_attach_mem_model(xdp_rxq, &mem);
}
EXPORT_SYMBOL_GPL(xdp_rxq_info_attach_page_pool);

/* XDP RX runs under NAPI protection, and in different delivery error
 * scenarios (e.g. queue full), it is possible to return the xdp_frame
 * while still leveraging this protection.  The @napi_direct boolean
 * is used for those calls sites.  Thus, allowing for faster recycling
 * of xdp_frames/pages in those cases.
 */
void __xdp_return(netmem_ref netmem, enum xdp_mem_type mem_type,
		  bool napi_direct, struct xdp_buff *xdp)
{
	switch (mem_type) {
	case MEM_TYPE_PAGE_POOL:
		netmem = netmem_compound_head(netmem);
		if (napi_direct && xdp_return_frame_no_direct())
			napi_direct = false;
		/* No need to check netmem_is_pp() as mem->type knows this a
		 * page_pool page
		 */
		page_pool_put_full_netmem(netmem_get_pp(netmem), netmem,
					  napi_direct);
		break;
	case MEM_TYPE_PAGE_SHARED:
		page_frag_free(__netmem_address(netmem));
		break;
	case MEM_TYPE_PAGE_ORDER0:
		put_page(__netmem_to_page(netmem));
		break;
	case MEM_TYPE_XSK_BUFF_POOL:
		/* NB! Only valid from an xdp_buff! */
		xsk_buff_free(xdp);
		break;
	default:
		/* Not possible, checked in xdp_rxq_info_reg_mem_model() */
		WARN(1, "Incorrect XDP memory type (%d) usage", mem_type);
		break;
	}
}

void xdp_return_frame(struct xdp_frame *xdpf)
{
	struct skb_shared_info *sinfo;

	if (likely(!xdp_frame_has_frags(xdpf)))
		goto out;

	sinfo = xdp_get_shared_info_from_frame(xdpf);
	for (u32 i = 0; i < sinfo->nr_frags; i++)
		__xdp_return(skb_frag_netmem(&sinfo->frags[i]), xdpf->mem_type,
			     false, NULL);

out:
	__xdp_return(virt_to_netmem(xdpf->data), xdpf->mem_type, false, NULL);
}
EXPORT_SYMBOL_GPL(xdp_return_frame);

void xdp_return_frame_rx_napi(struct xdp_frame *xdpf)
{
	struct skb_shared_info *sinfo;

	if (likely(!xdp_frame_has_frags(xdpf)))
		goto out;

	sinfo = xdp_get_shared_info_from_frame(xdpf);
	for (u32 i = 0; i < sinfo->nr_frags; i++)
		__xdp_return(skb_frag_netmem(&sinfo->frags[i]), xdpf->mem_type,
			     true, NULL);

out:
	__xdp_return(virt_to_netmem(xdpf->data), xdpf->mem_type, true, NULL);
}
EXPORT_SYMBOL_GPL(xdp_return_frame_rx_napi);

/* XDP bulk APIs introduce a defer/flush mechanism to return
 * pages belonging to the same xdp_mem_allocator object
 * (identified via the mem.id field) in bulk to optimize
 * I-cache and D-cache.
 * The bulk queue size is set to 16 to be aligned to how
 * XDP_REDIRECT bulking works. The bulk is flushed when
 * it is full or when mem.id changes.
 * xdp_frame_bulk is usually stored/allocated on the function
 * call-stack to avoid locking penalties.
 */

/* Must be called with rcu_read_lock held */
void xdp_return_frame_bulk(struct xdp_frame *xdpf,
			   struct xdp_frame_bulk *bq)
{
	if (xdpf->mem_type != MEM_TYPE_PAGE_POOL) {
		xdp_return_frame(xdpf);
		return;
	}

	if (bq->count == XDP_BULK_QUEUE_SIZE)
		xdp_flush_frame_bulk(bq);

	if (unlikely(xdp_frame_has_frags(xdpf))) {
		struct skb_shared_info *sinfo;
		int i;

		sinfo = xdp_get_shared_info_from_frame(xdpf);
		for (i = 0; i < sinfo->nr_frags; i++) {
			skb_frag_t *frag = &sinfo->frags[i];

			bq->q[bq->count++] = skb_frag_netmem(frag);
			if (bq->count == XDP_BULK_QUEUE_SIZE)
				xdp_flush_frame_bulk(bq);
		}
	}
	bq->q[bq->count++] = virt_to_netmem(xdpf->data);
}
EXPORT_SYMBOL_GPL(xdp_return_frame_bulk);

/**
 * xdp_return_frag -- free one XDP frag or decrement its refcount
 * @netmem: network memory reference to release
 * @xdp: &xdp_buff to release the frag for
 */
void xdp_return_frag(netmem_ref netmem, const struct xdp_buff *xdp)
{
	__xdp_return(netmem, xdp->rxq->mem.type, true, NULL);
}
EXPORT_SYMBOL_GPL(xdp_return_frag);

void xdp_return_buff(struct xdp_buff *xdp)
{
	struct skb_shared_info *sinfo;

	if (likely(!xdp_buff_has_frags(xdp)))
		goto out;

	sinfo = xdp_get_shared_info_from_buff(xdp);
	for (u32 i = 0; i < sinfo->nr_frags; i++)
		__xdp_return(skb_frag_netmem(&sinfo->frags[i]),
			     xdp->rxq->mem.type, true, xdp);

out:
	__xdp_return(virt_to_netmem(xdp->data), xdp->rxq->mem.type, true, xdp);
}
EXPORT_SYMBOL_GPL(xdp_return_buff);

void xdp_attachment_setup(struct xdp_attachment_info *info,
			  struct netdev_bpf *bpf)
{
	if (info->prog)
		bpf_prog_put(info->prog);
	info->prog = bpf->prog;
	info->flags = bpf->flags;
}
EXPORT_SYMBOL_GPL(xdp_attachment_setup);

struct xdp_frame *xdp_convert_zc_to_xdp_frame(struct xdp_buff *xdp)
{
	unsigned int metasize, totsize;
	void *addr, *data_to_copy;
	struct xdp_frame *xdpf;
	struct page *page;

	/* Clone into a MEM_TYPE_PAGE_ORDER0 xdp_frame. */
	metasize = xdp_data_meta_unsupported(xdp) ? 0 :
		   xdp->data - xdp->data_meta;
	totsize = xdp->data_end - xdp->data + metasize;

	if (sizeof(*xdpf) + totsize > PAGE_SIZE)
		return NULL;

	page = dev_alloc_page();
	if (!page)
		return NULL;

	addr = page_to_virt(page);
	xdpf = addr;
	memset(xdpf, 0, sizeof(*xdpf));

	addr += sizeof(*xdpf);
	data_to_copy = metasize ? xdp->data_meta : xdp->data;
	memcpy(addr, data_to_copy, totsize);

	xdpf->data = addr + metasize;
	xdpf->len = totsize - metasize;
	xdpf->headroom = 0;
	xdpf->metasize = metasize;
	xdpf->frame_sz = PAGE_SIZE;
	xdpf->mem_type = MEM_TYPE_PAGE_ORDER0;

	xsk_buff_free(xdp);
	return xdpf;
}
EXPORT_SYMBOL_GPL(xdp_convert_zc_to_xdp_frame);

/* Used by XDP_WARN macro, to avoid inlining WARN() in fast-path */
void xdp_warn(const char *msg, const char *func, const int line)
{
	WARN(1, "XDP_WARN: %s(line:%d): %s\n", func, line, msg);
};
EXPORT_SYMBOL_GPL(xdp_warn);

/**
 * xdp_build_skb_from_buff - create an skb from &xdp_buff
 * @xdp: &xdp_buff to convert to an skb
 *
 * Perform common operations to create a new skb to pass up the stack from
 * &xdp_buff: allocate an skb head from the NAPI percpu cache, initialize
 * skb data pointers and offsets, set the recycle bit if the buff is
 * PP-backed, Rx queue index, protocol and update frags info.
 *
 * Return: new &sk_buff on success, %NULL on error.
 */
struct sk_buff *xdp_build_skb_from_buff(const struct xdp_buff *xdp)
{
	const struct xdp_rxq_info *rxq = xdp->rxq;
	const struct skb_shared_info *sinfo;
	struct sk_buff *skb;
	u32 nr_frags = 0;
	int metalen;

	if (unlikely(xdp_buff_has_frags(xdp))) {
		sinfo = xdp_get_shared_info_from_buff(xdp);
		nr_frags = sinfo->nr_frags;
	}

	skb = napi_build_skb(xdp->data_hard_start, xdp->frame_sz);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	__skb_put(skb, xdp->data_end - xdp->data);

	metalen = xdp->data - xdp->data_meta;
	if (metalen > 0)
		skb_metadata_set(skb, metalen);

	if (rxq->mem.type == MEM_TYPE_PAGE_POOL)
		skb_mark_for_recycle(skb);

	skb_record_rx_queue(skb, rxq->queue_index);

	if (unlikely(nr_frags)) {
		u32 tsize;

		tsize = sinfo->xdp_frags_truesize ? : nr_frags * xdp->frame_sz;
		xdp_update_skb_frags_info(skb, nr_frags, sinfo->xdp_frags_size,
					  tsize, xdp_buff_get_skb_flags(xdp));
	}

	skb->protocol = eth_type_trans(skb, rxq->dev);

	return skb;
}
EXPORT_SYMBOL_GPL(xdp_build_skb_from_buff);

/**
 * xdp_copy_frags_from_zc - copy frags from XSk buff to skb
 * @skb: skb to copy frags to
 * @xdp: XSk &xdp_buff from which the frags will be copied
 * @pp: &page_pool backing page allocation, if available
 *
 * Copy all frags from XSk &xdp_buff to the skb to pass it up the stack.
 * Allocate a new buffer for each frag, copy it and attach to the skb.
 *
 * Return: true on success, false on netmem allocation fail.
 */
static noinline bool xdp_copy_frags_from_zc(struct sk_buff *skb,
					    const struct xdp_buff *xdp,
					    struct page_pool *pp)
{
	struct skb_shared_info *sinfo = skb_shinfo(skb);
	const struct skb_shared_info *xinfo;
	u32 nr_frags, tsize = 0;
	u32 flags = 0;

	xinfo = xdp_get_shared_info_from_buff(xdp);
	nr_frags = xinfo->nr_frags;

	for (u32 i = 0; i < nr_frags; i++) {
		const skb_frag_t *frag = &xinfo->frags[i];
		u32 len = skb_frag_size(frag);
		u32 offset, truesize = len;
		struct page *page;

		page = page_pool_dev_alloc(pp, &offset, &truesize);
		if (unlikely(!page)) {
			sinfo->nr_frags = i;
			return false;
		}

		memcpy(page_address(page) + offset, skb_frag_address(frag),
		       LARGEST_ALIGN(len));
		__skb_fill_page_desc_noacc(sinfo, i, page, offset, len);

		tsize += truesize;
		if (page_is_pfmemalloc(page))
			flags |= XDP_FLAGS_FRAGS_PF_MEMALLOC;
	}

	xdp_update_skb_frags_info(skb, nr_frags, xinfo->xdp_frags_size, tsize,
				  flags);

	return true;
}

/**
 * xdp_build_skb_from_zc - create an skb from XSk &xdp_buff
 * @xdp: source XSk buff
 *
 * Similar to xdp_build_skb_from_buff(), but for XSk frames. Allocate an skb
 * head, new buffer for the head, copy the data and initialize the skb fields.
 * If there are frags, allocate new buffers for them and copy.
 * Buffers are allocated from the system percpu pools to try recycling them.
 * If new skb was built successfully, @xdp is returned to XSk pool's freelist.
 * On error, it remains untouched and the caller must take care of this.
 *
 * Return: new &sk_buff on success, %NULL on error.
 */
struct sk_buff *xdp_build_skb_from_zc(struct xdp_buff *xdp)
{
	const struct xdp_rxq_info *rxq = xdp->rxq;
	u32 len = xdp->data_end - xdp->data_meta;
	u32 truesize = xdp->frame_sz;
	struct sk_buff *skb = NULL;
	struct page_pool *pp;
	int metalen;
	void *data;

	if (!IS_ENABLED(CONFIG_PAGE_POOL))
		return NULL;

	local_lock_nested_bh(&system_page_pool.bh_lock);
	pp = this_cpu_read(system_page_pool.pool);
	data = page_pool_dev_alloc_va(pp, &truesize);
	if (unlikely(!data))
		goto out;

	skb = napi_build_skb(data, truesize);
	if (unlikely(!skb)) {
		page_pool_free_va(pp, data, true);
		goto out;
	}

	skb_mark_for_recycle(skb);
	skb_reserve(skb, xdp->data_meta - xdp->data_hard_start);

	memcpy(__skb_put(skb, len), xdp->data_meta, LARGEST_ALIGN(len));

	metalen = xdp->data - xdp->data_meta;
	if (metalen > 0) {
		skb_metadata_set(skb, metalen);
		__skb_pull(skb, metalen);
	}

	skb_record_rx_queue(skb, rxq->queue_index);

	if (unlikely(xdp_buff_has_frags(xdp)) &&
	    unlikely(!xdp_copy_frags_from_zc(skb, xdp, pp))) {
		napi_consume_skb(skb, true);
		skb = NULL;
		goto out;
	}

	xsk_buff_free(xdp);

	skb->protocol = eth_type_trans(skb, rxq->dev);

out:
	local_unlock_nested_bh(&system_page_pool.bh_lock);
	return skb;
}
EXPORT_SYMBOL_GPL(xdp_build_skb_from_zc);

struct sk_buff *__xdp_build_skb_from_frame(struct xdp_frame *xdpf,
					   struct sk_buff *skb,
					   struct net_device *dev)
{
	struct skb_shared_info *sinfo = xdp_get_shared_info_from_frame(xdpf);
	unsigned int headroom, frame_size;
	void *hard_start;
	u8 nr_frags;

	/* xdp frags frame */
	if (unlikely(xdp_frame_has_frags(xdpf)))
		nr_frags = sinfo->nr_frags;

	/* Part of headroom was reserved to xdpf */
	headroom = sizeof(*xdpf) + xdpf->headroom;

	/* Memory size backing xdp_frame data already have reserved
	 * room for build_skb to place skb_shared_info in tailroom.
	 */
	frame_size = xdpf->frame_sz;

	hard_start = xdpf->data - headroom;
	skb = build_skb_around(skb, hard_start, frame_size);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, headroom);
	__skb_put(skb, xdpf->len);
	if (xdpf->metasize)
		skb_metadata_set(skb, xdpf->metasize);

	if (unlikely(xdp_frame_has_frags(xdpf)))
		xdp_update_skb_frags_info(skb, nr_frags, sinfo->xdp_frags_size,
					  nr_frags * xdpf->frame_sz,
					  xdp_frame_get_skb_flags(xdpf));

	/* Essential SKB info: protocol and skb->dev */
	skb->protocol = eth_type_trans(skb, dev);

	/* Optional SKB info, currently missing:
	 * - HW checksum info		(skb->ip_summed)
	 * - HW RX hash			(skb_set_hash)
	 * - RX ring dev queue index	(skb_record_rx_queue)
	 */

	if (xdpf->mem_type == MEM_TYPE_PAGE_POOL)
		skb_mark_for_recycle(skb);

	/* Allow SKB to reuse area used by xdp_frame */
	xdp_scrub_frame(xdpf);

	return skb;
}
EXPORT_SYMBOL_GPL(__xdp_build_skb_from_frame);

struct sk_buff *xdp_build_skb_from_frame(struct xdp_frame *xdpf,
					 struct net_device *dev)
{
	struct sk_buff *skb;

	skb = kmem_cache_alloc(net_hotdata.skbuff_cache, GFP_ATOMIC);
	if (unlikely(!skb))
		return NULL;

	memset(skb, 0, offsetof(struct sk_buff, tail));

	return __xdp_build_skb_from_frame(xdpf, skb, dev);
}
EXPORT_SYMBOL_GPL(xdp_build_skb_from_frame);

struct xdp_frame *xdpf_clone(struct xdp_frame *xdpf)
{
	unsigned int headroom, totalsize;
	struct xdp_frame *nxdpf;
	struct page *page;
	void *addr;

	headroom = xdpf->headroom + sizeof(*xdpf);
	totalsize = headroom + xdpf->len;

	if (unlikely(totalsize > PAGE_SIZE))
		return NULL;
	page = dev_alloc_page();
	if (!page)
		return NULL;
	addr = page_to_virt(page);

	memcpy(addr, xdpf, totalsize);

	nxdpf = addr;
	nxdpf->data = addr + headroom;
	nxdpf->frame_sz = PAGE_SIZE;
	nxdpf->mem_type = MEM_TYPE_PAGE_ORDER0;

	return nxdpf;
}

__bpf_kfunc_start_defs();

/**
 * bpf_xdp_metadata_rx_timestamp - Read XDP frame RX timestamp.
 * @ctx: XDP context pointer.
 * @timestamp: Return value pointer.
 *
 * Return:
 * * Returns 0 on success or ``-errno`` on error.
 * * ``-EOPNOTSUPP`` : means device driver does not implement kfunc
 * * ``-ENODATA``    : means no RX-timestamp available for this frame
 */
__bpf_kfunc int bpf_xdp_metadata_rx_timestamp(const struct xdp_md *ctx, u64 *timestamp)
{
	return -EOPNOTSUPP;
}

/**
 * bpf_xdp_metadata_rx_hash - Read XDP frame RX hash.
 * @ctx: XDP context pointer.
 * @hash: Return value pointer.
 * @rss_type: Return value pointer for RSS type.
 *
 * The RSS hash type (@rss_type) specifies what portion of packet headers NIC
 * hardware used when calculating RSS hash value.  The RSS type can be decoded
 * via &enum xdp_rss_hash_type either matching on individual L3/L4 bits
 * ``XDP_RSS_L*`` or by combined traditional *RSS Hashing Types*
 * ``XDP_RSS_TYPE_L*``.
 *
 * Return:
 * * Returns 0 on success or ``-errno`` on error.
 * * ``-EOPNOTSUPP`` : means device driver doesn't implement kfunc
 * * ``-ENODATA``    : means no RX-hash available for this frame
 */
__bpf_kfunc int bpf_xdp_metadata_rx_hash(const struct xdp_md *ctx, u32 *hash,
					 enum xdp_rss_hash_type *rss_type)
{
	return -EOPNOTSUPP;
}

/**
 * bpf_xdp_metadata_rx_vlan_tag - Get XDP packet outermost VLAN tag
 * @ctx: XDP context pointer.
 * @vlan_proto: Destination pointer for VLAN Tag protocol identifier (TPID).
 * @vlan_tci: Destination pointer for VLAN TCI (VID + DEI + PCP)
 *
 * In case of success, ``vlan_proto`` contains *Tag protocol identifier (TPID)*,
 * usually ``ETH_P_8021Q`` or ``ETH_P_8021AD``, but some networks can use
 * custom TPIDs. ``vlan_proto`` is stored in **network byte order (BE)**
 * and should be used as follows:
 * ``if (vlan_proto == bpf_htons(ETH_P_8021Q)) do_something();``
 *
 * ``vlan_tci`` contains the remaining 16 bits of a VLAN tag.
 * Driver is expected to provide those in **host byte order (usually LE)**,
 * so the bpf program should not perform byte conversion.
 * According to 802.1Q standard, *VLAN TCI (Tag control information)*
 * is a bit field that contains:
 * *VLAN identifier (VID)* that can be read with ``vlan_tci & 0xfff``,
 * *Drop eligible indicator (DEI)* - 1 bit,
 * *Priority code point (PCP)* - 3 bits.
 * For detailed meaning of DEI and PCP, please refer to other sources.
 *
 * Return:
 * * Returns 0 on success or ``-errno`` on error.
 * * ``-EOPNOTSUPP`` : device driver doesn't implement kfunc
 * * ``-ENODATA``    : VLAN tag was not stripped or is not available
 */
__bpf_kfunc int bpf_xdp_metadata_rx_vlan_tag(const struct xdp_md *ctx,
					     __be16 *vlan_proto, u16 *vlan_tci)
{
	return -EOPNOTSUPP;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(xdp_metadata_kfunc_ids)
#define XDP_METADATA_KFUNC(_, __, name, ___) BTF_ID_FLAGS(func, name, KF_TRUSTED_ARGS)
XDP_METADATA_KFUNC_xxx
#undef XDP_METADATA_KFUNC
BTF_KFUNCS_END(xdp_metadata_kfunc_ids)

static const struct btf_kfunc_id_set xdp_metadata_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &xdp_metadata_kfunc_ids,
};

BTF_ID_LIST(xdp_metadata_kfunc_ids_unsorted)
#define XDP_METADATA_KFUNC(name, _, str, __) BTF_ID(func, str)
XDP_METADATA_KFUNC_xxx
#undef XDP_METADATA_KFUNC

u32 bpf_xdp_metadata_kfunc_id(int id)
{
	/* xdp_metadata_kfunc_ids is sorted and can't be used */
	return xdp_metadata_kfunc_ids_unsorted[id];
}

bool bpf_dev_bound_kfunc_id(u32 btf_id)
{
	return btf_id_set8_contains(&xdp_metadata_kfunc_ids, btf_id);
}

static int __init xdp_metadata_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP, &xdp_metadata_kfunc_set);
}
late_initcall(xdp_metadata_init);

void xdp_set_features_flag_locked(struct net_device *dev, xdp_features_t val)
{
	val &= NETDEV_XDP_ACT_MASK;
	if (dev->xdp_features == val)
		return;

	netdev_assert_locked_or_invisible(dev);
	dev->xdp_features = val;

	if (dev->reg_state == NETREG_REGISTERED)
		call_netdevice_notifiers(NETDEV_XDP_FEAT_CHANGE, dev);
}
EXPORT_SYMBOL_GPL(xdp_set_features_flag_locked);

void xdp_set_features_flag(struct net_device *dev, xdp_features_t val)
{
	netdev_lock(dev);
	xdp_set_features_flag_locked(dev, val);
	netdev_unlock(dev);
}
EXPORT_SYMBOL_GPL(xdp_set_features_flag);

void xdp_features_set_redirect_target_locked(struct net_device *dev,
					     bool support_sg)
{
	xdp_features_t val = (dev->xdp_features | NETDEV_XDP_ACT_NDO_XMIT);

	if (support_sg)
		val |= NETDEV_XDP_ACT_NDO_XMIT_SG;
	xdp_set_features_flag_locked(dev, val);
}
EXPORT_SYMBOL_GPL(xdp_features_set_redirect_target_locked);

void xdp_features_set_redirect_target(struct net_device *dev, bool support_sg)
{
	netdev_lock(dev);
	xdp_features_set_redirect_target_locked(dev, support_sg);
	netdev_unlock(dev);
}
EXPORT_SYMBOL_GPL(xdp_features_set_redirect_target);

void xdp_features_clear_redirect_target_locked(struct net_device *dev)
{
	xdp_features_t val = dev->xdp_features;

	val &= ~(NETDEV_XDP_ACT_NDO_XMIT | NETDEV_XDP_ACT_NDO_XMIT_SG);
	xdp_set_features_flag_locked(dev, val);
}
EXPORT_SYMBOL_GPL(xdp_features_clear_redirect_target_locked);

void xdp_features_clear_redirect_target(struct net_device *dev)
{
	netdev_lock(dev);
	xdp_features_clear_redirect_target_locked(dev);
	netdev_unlock(dev);
}
EXPORT_SYMBOL_GPL(xdp_features_clear_redirect_target);
