// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Contiguous Memory Allocator
 *
 * Copyright (c) 2010-2011 by Samsung Electronics.
 * Copyright IBM Corporation, 2013
 * Copyright LG Electronics Inc., 2014
 * Written by:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *	Michal Nazarewicz <mina86@mina86.com>
 *	Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *	Joonsoo Kim <iamjoonsoo.kim@lge.com>
 */

#define pr_fmt(fmt) "cma: " fmt

#define CREATE_TRACE_POINTS

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string_choices.h>
#include <linux/log2.h>
#include <linux/cma.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/kmemleak.h>
#include <trace/events/cma.h>

#include "internal.h"
#include "cma.h"

struct cma cma_areas[MAX_CMA_AREAS];
unsigned int cma_area_count;

phys_addr_t cma_get_base(const struct cma *cma)
{
	WARN_ON_ONCE(cma->nranges != 1);
	return PFN_PHYS(cma->ranges[0].base_pfn);
}

unsigned long cma_get_size(const struct cma *cma)
{
	return cma->count << PAGE_SHIFT;
}

const char *cma_get_name(const struct cma *cma)
{
	return cma->name;
}

static unsigned long cma_bitmap_aligned_mask(const struct cma *cma,
					     unsigned int align_order)
{
	if (align_order <= cma->order_per_bit)
		return 0;
	return (1UL << (align_order - cma->order_per_bit)) - 1;
}

/*
 * Find the offset of the base PFN from the specified align_order.
 * The value returned is represented in order_per_bits.
 */
static unsigned long cma_bitmap_aligned_offset(const struct cma *cma,
					       const struct cma_memrange *cmr,
					       unsigned int align_order)
{
	return (cmr->base_pfn & ((1UL << align_order) - 1))
		>> cma->order_per_bit;
}

static unsigned long cma_bitmap_pages_to_bits(const struct cma *cma,
					      unsigned long pages)
{
	return ALIGN(pages, 1UL << cma->order_per_bit) >> cma->order_per_bit;
}

static void cma_clear_bitmap(struct cma *cma, const struct cma_memrange *cmr,
			     unsigned long pfn, unsigned long count)
{
	unsigned long bitmap_no, bitmap_count;
	unsigned long flags;

	bitmap_no = (pfn - cmr->base_pfn) >> cma->order_per_bit;
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	spin_lock_irqsave(&cma->lock, flags);
	bitmap_clear(cmr->bitmap, bitmap_no, bitmap_count);
	cma->available_count += count;
	spin_unlock_irqrestore(&cma->lock, flags);
}

/*
 * Check if a CMA area contains no ranges that intersect with
 * multiple zones. Store the result in the flags in case
 * this gets called more than once.
 */
bool cma_validate_zones(struct cma *cma)
{
	int r;
	unsigned long base_pfn;
	struct cma_memrange *cmr;
	bool valid_bit_set;

	/*
	 * If already validated, return result of previous check.
	 * Either the valid or invalid bit will be set if this
	 * check has already been done. If neither is set, the
	 * check has not been performed yet.
	 */
	valid_bit_set = test_bit(CMA_ZONES_VALID, &cma->flags);
	if (valid_bit_set || test_bit(CMA_ZONES_INVALID, &cma->flags))
		return valid_bit_set;

	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];
		base_pfn = cmr->base_pfn;

		/*
		 * alloc_contig_range() requires the pfn range specified
		 * to be in the same zone. Simplify by forcing the entire
		 * CMA resv range to be in the same zone.
		 */
		WARN_ON_ONCE(!pfn_valid(base_pfn));
		if (pfn_range_intersects_zones(cma->nid, base_pfn, cmr->count)) {
			set_bit(CMA_ZONES_INVALID, &cma->flags);
			return false;
		}
	}

	set_bit(CMA_ZONES_VALID, &cma->flags);

	return true;
}

static void __init cma_activate_area(struct cma *cma)
{
	unsigned long pfn, end_pfn, early_pfn[CMA_MAX_RANGES];
	int allocrange, r;
	struct cma_memrange *cmr;
	unsigned long bitmap_count, count;

	for (allocrange = 0; allocrange < cma->nranges; allocrange++) {
		cmr = &cma->ranges[allocrange];
		early_pfn[allocrange] = cmr->early_pfn;
		cmr->bitmap = bitmap_zalloc(cma_bitmap_maxno(cma, cmr),
					    GFP_KERNEL);
		if (!cmr->bitmap)
			goto cleanup;
	}

	if (!cma_validate_zones(cma))
		goto cleanup;

	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];
		if (early_pfn[r] != cmr->base_pfn) {
			count = early_pfn[r] - cmr->base_pfn;
			bitmap_count = cma_bitmap_pages_to_bits(cma, count);
			bitmap_set(cmr->bitmap, 0, bitmap_count);
		}

		for (pfn = early_pfn[r]; pfn < cmr->base_pfn + cmr->count;
		     pfn += pageblock_nr_pages)
			init_cma_reserved_pageblock(pfn_to_page(pfn));
	}

	spin_lock_init(&cma->lock);

	mutex_init(&cma->alloc_mutex);

#ifdef CONFIG_CMA_DEBUGFS
	INIT_HLIST_HEAD(&cma->mem_head);
	spin_lock_init(&cma->mem_head_lock);
#endif
	set_bit(CMA_ACTIVATED, &cma->flags);

	return;

cleanup:
	for (r = 0; r < allocrange; r++)
		bitmap_free(cma->ranges[r].bitmap);

	/* Expose all pages to the buddy, they are useless for CMA. */
	if (!test_bit(CMA_RESERVE_PAGES_ON_ERROR, &cma->flags)) {
		for (r = 0; r < allocrange; r++) {
			cmr = &cma->ranges[r];
			end_pfn = cmr->base_pfn + cmr->count;
			for (pfn = early_pfn[r]; pfn < end_pfn; pfn++)
				free_reserved_page(pfn_to_page(pfn));
		}
	}
	totalcma_pages -= cma->count;
	cma->available_count = cma->count = 0;
	pr_err("CMA area %s could not be activated\n", cma->name);
}

static int __init cma_init_reserved_areas(void)
{
	int i;

	for (i = 0; i < cma_area_count; i++)
		cma_activate_area(&cma_areas[i]);

	return 0;
}
core_initcall(cma_init_reserved_areas);

void __init cma_reserve_pages_on_error(struct cma *cma)
{
	set_bit(CMA_RESERVE_PAGES_ON_ERROR, &cma->flags);
}

static int __init cma_new_area(const char *name, phys_addr_t size,
			       unsigned int order_per_bit,
			       struct cma **res_cma)
{
	struct cma *cma;

	if (cma_area_count == ARRAY_SIZE(cma_areas)) {
		pr_err("Not enough slots for CMA reserved regions!\n");
		return -ENOSPC;
	}

	/*
	 * Each reserved area must be initialised later, when more kernel
	 * subsystems (like slab allocator) are available.
	 */
	cma = &cma_areas[cma_area_count];
	cma_area_count++;

	if (name)
		snprintf(cma->name, CMA_MAX_NAME, "%s", name);
	else
		snprintf(cma->name, CMA_MAX_NAME,  "cma%d\n", cma_area_count);

	cma->available_count = cma->count = size >> PAGE_SHIFT;
	cma->order_per_bit = order_per_bit;
	*res_cma = cma;
	totalcma_pages += cma->count;

	return 0;
}

static void __init cma_drop_area(struct cma *cma)
{
	totalcma_pages -= cma->count;
	cma_area_count--;
}

/**
 * cma_init_reserved_mem() - create custom contiguous area from reserved memory
 * @base: Base address of the reserved area
 * @size: Size of the reserved area (in bytes),
 * @order_per_bit: Order of pages represented by one bit on bitmap.
 * @name: The name of the area. If this parameter is NULL, the name of
 *        the area will be set to "cmaN", where N is a running counter of
 *        used areas.
 * @res_cma: Pointer to store the created cma region.
 *
 * This function creates custom contiguous area from already reserved memory.
 */
int __init cma_init_reserved_mem(phys_addr_t base, phys_addr_t size,
				 unsigned int order_per_bit,
				 const char *name,
				 struct cma **res_cma)
{
	struct cma *cma;
	int ret;

	/* Sanity checks */
	if (!size || !memblock_is_region_reserved(base, size))
		return -EINVAL;

	/*
	 * CMA uses CMA_MIN_ALIGNMENT_BYTES as alignment requirement which
	 * needs pageblock_order to be initialized. Let's enforce it.
	 */
	if (!pageblock_order) {
		pr_err("pageblock_order not yet initialized. Called during early boot?\n");
		return -EINVAL;
	}

	/* ensure minimal alignment required by mm core */
	if (!IS_ALIGNED(base | size, CMA_MIN_ALIGNMENT_BYTES))
		return -EINVAL;

	ret = cma_new_area(name, size, order_per_bit, &cma);
	if (ret != 0)
		return ret;

	cma->ranges[0].base_pfn = PFN_DOWN(base);
	cma->ranges[0].early_pfn = PFN_DOWN(base);
	cma->ranges[0].count = cma->count;
	cma->nranges = 1;
	cma->nid = NUMA_NO_NODE;

	*res_cma = cma;

	return 0;
}

/*
 * Structure used while walking physical memory ranges and finding out
 * which one(s) to use for a CMA area.
 */
struct cma_init_memrange {
	phys_addr_t base;
	phys_addr_t size;
	struct list_head list;
};

/*
 * Work array used during CMA initialization.
 */
static struct cma_init_memrange memranges[CMA_MAX_RANGES] __initdata;

static bool __init revsizecmp(struct cma_init_memrange *mlp,
			      struct cma_init_memrange *mrp)
{
	return mlp->size > mrp->size;
}

static bool __init basecmp(struct cma_init_memrange *mlp,
			   struct cma_init_memrange *mrp)
{
	return mlp->base < mrp->base;
}

/*
 * Helper function to create sorted lists.
 */
static void __init list_insert_sorted(
	struct list_head *ranges,
	struct cma_init_memrange *mrp,
	bool (*cmp)(struct cma_init_memrange *lh, struct cma_init_memrange *rh))
{
	struct list_head *mp;
	struct cma_init_memrange *mlp;

	if (list_empty(ranges))
		list_add(&mrp->list, ranges);
	else {
		list_for_each(mp, ranges) {
			mlp = list_entry(mp, struct cma_init_memrange, list);
			if (cmp(mlp, mrp))
				break;
		}
		__list_add(&mrp->list, mlp->list.prev, &mlp->list);
	}
}

static int __init cma_fixed_reserve(phys_addr_t base, phys_addr_t size)
{
	if (IS_ENABLED(CONFIG_HIGHMEM)) {
		phys_addr_t highmem_start = __pa(high_memory - 1) + 1;

		/*
		 * If allocating at a fixed base the request region must not
		 * cross the low/high memory boundary.
		 */
		if (base < highmem_start && base + size > highmem_start) {
			pr_err("Region at %pa defined on low/high memory boundary (%pa)\n",
			       &base, &highmem_start);
			return -EINVAL;
		}
	}

	if (memblock_is_region_reserved(base, size) ||
	    memblock_reserve(base, size) < 0) {
		return -EBUSY;
	}

	return 0;
}

static phys_addr_t __init cma_alloc_mem(phys_addr_t base, phys_addr_t size,
			phys_addr_t align, phys_addr_t limit, int nid)
{
	phys_addr_t addr = 0;

	/*
	 * If there is enough memory, try a bottom-up allocation first.
	 * It will place the new cma area close to the start of the node
	 * and guarantee that the compaction is moving pages out of the
	 * cma area and not into it.
	 * Avoid using first 4GB to not interfere with constrained zones
	 * like DMA/DMA32.
	 */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
	if (!memblock_bottom_up() && limit >= SZ_4G + size) {
		memblock_set_bottom_up(true);
		addr = memblock_alloc_range_nid(size, align, SZ_4G, limit,
						nid, true);
		memblock_set_bottom_up(false);
	}
#endif

	/*
	 * On systems with HIGHMEM try allocating from there before consuming
	 * memory in lower zones.
	 */
	if (!addr && IS_ENABLED(CONFIG_HIGHMEM)) {
		phys_addr_t highmem = __pa(high_memory - 1) + 1;

		/*
		 * All pages in the reserved area must come from the same zone.
		 * If the requested region crosses the low/high memory boundary,
		 * try allocating from high memory first and fall back to low
		 * memory in case of failure.
		 */
		if (base < highmem && limit > highmem) {
			addr = memblock_alloc_range_nid(size, align, highmem,
							limit, nid, true);
			limit = highmem;
		}
	}

	if (!addr)
		addr = memblock_alloc_range_nid(size, align, base, limit, nid,
						true);

	return addr;
}

static int __init __cma_declare_contiguous_nid(phys_addr_t *basep,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, const char *name, struct cma **res_cma,
			int nid)
{
	phys_addr_t memblock_end = memblock_end_of_DRAM();
	phys_addr_t base = *basep;
	int ret;

	pr_debug("%s(size %pa, base %pa, limit %pa alignment %pa)\n",
		__func__, &size, &base, &limit, &alignment);

	if (cma_area_count == ARRAY_SIZE(cma_areas)) {
		pr_err("Not enough slots for CMA reserved regions!\n");
		return -ENOSPC;
	}

	if (!size)
		return -EINVAL;

	if (alignment && !is_power_of_2(alignment))
		return -EINVAL;

	if (!IS_ENABLED(CONFIG_NUMA))
		nid = NUMA_NO_NODE;

	/* Sanitise input arguments. */
	alignment = max_t(phys_addr_t, alignment, CMA_MIN_ALIGNMENT_BYTES);
	if (fixed && base & (alignment - 1)) {
		pr_err("Region at %pa must be aligned to %pa bytes\n",
			&base, &alignment);
		return -EINVAL;
	}
	base = ALIGN(base, alignment);
	size = ALIGN(size, alignment);
	limit &= ~(alignment - 1);

	if (!base)
		fixed = false;

	/* size should be aligned with order_per_bit */
	if (!IS_ALIGNED(size >> PAGE_SHIFT, 1 << order_per_bit))
		return -EINVAL;


	/*
	 * If the limit is unspecified or above the memblock end, its effective
	 * value will be the memblock end. Set it explicitly to simplify further
	 * checks.
	 */
	if (limit == 0 || limit > memblock_end)
		limit = memblock_end;

	if (base + size > limit) {
		pr_err("Size (%pa) of region at %pa exceeds limit (%pa)\n",
			&size, &base, &limit);
		return -EINVAL;
	}

	/* Reserve memory */
	if (fixed) {
		ret = cma_fixed_reserve(base, size);
		if (ret)
			return ret;
	} else {
		base = cma_alloc_mem(base, size, alignment, limit, nid);
		if (!base)
			return -ENOMEM;

		/*
		 * kmemleak scans/reads tracked objects for pointers to other
		 * objects but this address isn't mapped and accessible
		 */
		kmemleak_ignore_phys(base);
	}

	ret = cma_init_reserved_mem(base, size, order_per_bit, name, res_cma);
	if (ret) {
		memblock_phys_free(base, size);
		return ret;
	}

	(*res_cma)->nid = nid;
	*basep = base;

	return 0;
}

/*
 * Create CMA areas with a total size of @total_size. A normal allocation
 * for one area is tried first. If that fails, the biggest memblock
 * ranges above 4G are selected, and allocated bottom up.
 *
 * The complexity here is not great, but this function will only be
 * called during boot, and the lists operated on have fewer than
 * CMA_MAX_RANGES elements (default value: 8).
 */
int __init cma_declare_contiguous_multi(phys_addr_t total_size,
			phys_addr_t align, unsigned int order_per_bit,
			const char *name, struct cma **res_cma, int nid)
{
	phys_addr_t start = 0, end;
	phys_addr_t size, sizesum, sizeleft;
	struct cma_init_memrange *mrp, *mlp, *failed;
	struct cma_memrange *cmrp;
	LIST_HEAD(ranges);
	LIST_HEAD(final_ranges);
	struct list_head *mp, *next;
	int ret, nr = 1;
	u64 i;
	struct cma *cma;

	/*
	 * First, try it the normal way, producing just one range.
	 */
	ret = __cma_declare_contiguous_nid(&start, total_size, 0, align,
			order_per_bit, false, name, res_cma, nid);
	if (ret != -ENOMEM)
		goto out;

	/*
	 * Couldn't find one range that fits our needs, so try multiple
	 * ranges.
	 *
	 * No need to do the alignment checks here, the call to
	 * cma_declare_contiguous_nid above would have caught
	 * any issues. With the checks, we know that:
	 *
	 * - @align is a power of 2
	 * - @align is >= pageblock alignment
	 * - @size is aligned to @align and to @order_per_bit
	 *
	 * So, as long as we create ranges that have a base
	 * aligned to @align, and a size that is aligned to
	 * both @align and @order_to_bit, things will work out.
	 */
	nr = 0;
	sizesum = 0;
	failed = NULL;

	ret = cma_new_area(name, total_size, order_per_bit, &cma);
	if (ret != 0)
		goto out;

	align = max_t(phys_addr_t, align, CMA_MIN_ALIGNMENT_BYTES);
	/*
	 * Create a list of ranges above 4G, largest range first.
	 */
	for_each_free_mem_range(i, nid, MEMBLOCK_NONE, &start, &end, NULL) {
		if (upper_32_bits(start) == 0)
			continue;

		start = ALIGN(start, align);
		if (start >= end)
			continue;

		end = ALIGN_DOWN(end, align);
		if (end <= start)
			continue;

		size = end - start;
		size = ALIGN_DOWN(size, (PAGE_SIZE << order_per_bit));
		if (!size)
			continue;
		sizesum += size;

		pr_debug("consider %016llx - %016llx\n", (u64)start, (u64)end);

		/*
		 * If we don't yet have used the maximum number of
		 * areas, grab a new one.
		 *
		 * If we can't use anymore, see if this range is not
		 * smaller than the smallest one already recorded. If
		 * not, re-use the smallest element.
		 */
		if (nr < CMA_MAX_RANGES)
			mrp = &memranges[nr++];
		else {
			mrp = list_last_entry(&ranges,
					      struct cma_init_memrange, list);
			if (size < mrp->size)
				continue;
			list_del(&mrp->list);
			sizesum -= mrp->size;
			pr_debug("deleted %016llx - %016llx from the list\n",
				(u64)mrp->base, (u64)mrp->base + size);
		}
		mrp->base = start;
		mrp->size = size;

		/*
		 * Now do a sorted insert.
		 */
		list_insert_sorted(&ranges, mrp, revsizecmp);
		pr_debug("added %016llx - %016llx to the list\n",
		    (u64)mrp->base, (u64)mrp->base + size);
		pr_debug("total size now %llu\n", (u64)sizesum);
	}

	/*
	 * There is not enough room in the CMA_MAX_RANGES largest
	 * ranges, so bail out.
	 */
	if (sizesum < total_size) {
		cma_drop_area(cma);
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Found ranges that provide enough combined space.
	 * Now, sorted them by address, smallest first, because we
	 * want to mimic a bottom-up memblock allocation.
	 */
	sizesum = 0;
	list_for_each_safe(mp, next, &ranges) {
		mlp = list_entry(mp, struct cma_init_memrange, list);
		list_del(mp);
		list_insert_sorted(&final_ranges, mlp, basecmp);
		sizesum += mlp->size;
		if (sizesum >= total_size)
			break;
	}

	/*
	 * Walk the final list, and add a CMA range for
	 * each range, possibly not using the last one fully.
	 */
	nr = 0;
	sizeleft = total_size;
	list_for_each(mp, &final_ranges) {
		mlp = list_entry(mp, struct cma_init_memrange, list);
		size = min(sizeleft, mlp->size);
		if (memblock_reserve(mlp->base, size)) {
			/*
			 * Unexpected error. Could go on to
			 * the next one, but just abort to
			 * be safe.
			 */
			failed = mlp;
			break;
		}

		pr_debug("created region %d: %016llx - %016llx\n",
		    nr, (u64)mlp->base, (u64)mlp->base + size);
		cmrp = &cma->ranges[nr++];
		cmrp->base_pfn = PHYS_PFN(mlp->base);
		cmrp->early_pfn = cmrp->base_pfn;
		cmrp->count = size >> PAGE_SHIFT;

		sizeleft -= size;
		if (sizeleft == 0)
			break;
	}

	if (failed) {
		list_for_each(mp, &final_ranges) {
			mlp = list_entry(mp, struct cma_init_memrange, list);
			if (mlp == failed)
				break;
			memblock_phys_free(mlp->base, mlp->size);
		}
		cma_drop_area(cma);
		ret = -ENOMEM;
		goto out;
	}

	cma->nranges = nr;
	cma->nid = nid;
	*res_cma = cma;

out:
	if (ret != 0)
		pr_err("Failed to reserve %lu MiB\n",
			(unsigned long)total_size / SZ_1M);
	else
		pr_info("Reserved %lu MiB in %d range%s\n",
			(unsigned long)total_size / SZ_1M, nr, str_plural(nr));
	return ret;
}

/**
 * cma_declare_contiguous_nid() - reserve custom contiguous area
 * @base: Base address of the reserved area optional, use 0 for any
 * @size: Size of the reserved area (in bytes),
 * @limit: End address of the reserved memory (optional, 0 for any).
 * @alignment: Alignment for the CMA area, should be power of 2 or zero
 * @order_per_bit: Order of pages represented by one bit on bitmap.
 * @fixed: hint about where to place the reserved area
 * @name: The name of the area. See function cma_init_reserved_mem()
 * @res_cma: Pointer to store the created cma region.
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the early allocator (memblock or bootmem)
 * has been activated and all other subsystems have already allocated/reserved
 * memory. This function allows to create custom reserved areas.
 *
 * If @fixed is true, reserve contiguous area at exactly @base.  If false,
 * reserve in range from @base to @limit.
 */
int __init cma_declare_contiguous_nid(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, const char *name, struct cma **res_cma,
			int nid)
{
	int ret;

	ret = __cma_declare_contiguous_nid(&base, size, limit, alignment,
			order_per_bit, fixed, name, res_cma, nid);
	if (ret != 0)
		pr_err("Failed to reserve %ld MiB\n",
				(unsigned long)size / SZ_1M);
	else
		pr_info("Reserved %ld MiB at %pa\n",
				(unsigned long)size / SZ_1M, &base);

	return ret;
}

static void cma_debug_show_areas(struct cma *cma)
{
	unsigned long start, end;
	unsigned long nr_part;
	unsigned long nbits;
	int r;
	struct cma_memrange *cmr;

	spin_lock_irq(&cma->lock);
	pr_info("number of available pages: ");
	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];

		nbits = cma_bitmap_maxno(cma, cmr);

		pr_info("range %d: ", r);
		for_each_clear_bitrange(start, end, cmr->bitmap, nbits) {
			nr_part = (end - start) << cma->order_per_bit;
			pr_cont("%s%lu@%lu", start ? "+" : "", nr_part, start);
		}
		pr_info("\n");
	}
	pr_cont("=> %lu free of %lu total pages\n", cma->available_count,
			cma->count);
	spin_unlock_irq(&cma->lock);
}

static int cma_range_alloc(struct cma *cma, struct cma_memrange *cmr,
				unsigned long count, unsigned int align,
				struct page **pagep, gfp_t gfp)
{
	unsigned long bitmap_maxno, bitmap_no, bitmap_count;
	unsigned long start, pfn, mask, offset;
	int ret = -EBUSY;
	struct page *page = NULL;

	mask = cma_bitmap_aligned_mask(cma, align);
	offset = cma_bitmap_aligned_offset(cma, cmr, align);
	bitmap_maxno = cma_bitmap_maxno(cma, cmr);
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	if (bitmap_count > bitmap_maxno)
		goto out;

	for (start = 0; ; start = bitmap_no + mask + 1) {
		spin_lock_irq(&cma->lock);
		/*
		 * If the request is larger than the available number
		 * of pages, stop right away.
		 */
		if (count > cma->available_count) {
			spin_unlock_irq(&cma->lock);
			break;
		}
		bitmap_no = bitmap_find_next_zero_area_off(cmr->bitmap,
				bitmap_maxno, start, bitmap_count, mask,
				offset);
		if (bitmap_no >= bitmap_maxno) {
			spin_unlock_irq(&cma->lock);
			break;
		}

		pfn = cmr->base_pfn + (bitmap_no << cma->order_per_bit);
		page = pfn_to_page(pfn);

		/*
		 * Do not hand out page ranges that are not contiguous, so
		 * callers can just iterate the pages without having to worry
		 * about these corner cases.
		 */
		if (!page_range_contiguous(page, count)) {
			spin_unlock_irq(&cma->lock);
			pr_warn_ratelimited("%s: %s: skipping incompatible area [0x%lx-0x%lx]",
					    __func__, cma->name, pfn, pfn + count - 1);
			continue;
		}

		bitmap_set(cmr->bitmap, bitmap_no, bitmap_count);
		cma->available_count -= count;
		/*
		 * It's safe to drop the lock here. We've marked this region for
		 * our exclusive use. If the migration fails we will take the
		 * lock again and unmark it.
		 */
		spin_unlock_irq(&cma->lock);

		mutex_lock(&cma->alloc_mutex);
		ret = alloc_contig_range(pfn, pfn + count, ACR_FLAGS_CMA, gfp);
		mutex_unlock(&cma->alloc_mutex);
		if (!ret)
			break;

		cma_clear_bitmap(cma, cmr, pfn, count);
		if (ret != -EBUSY)
			break;

		pr_debug("%s(): memory range at pfn 0x%lx %p is busy, retrying\n",
			 __func__, pfn, page);

		trace_cma_alloc_busy_retry(cma->name, pfn, page, count, align);
	}
out:
	if (!ret)
		*pagep = page;
	return ret;
}

static struct page *__cma_alloc(struct cma *cma, unsigned long count,
		       unsigned int align, gfp_t gfp)
{
	struct page *page = NULL;
	int ret = -ENOMEM, r;
	unsigned long i;
	const char *name = cma ? cma->name : NULL;

	if (!cma || !cma->count)
		return page;

	pr_debug("%s(cma %p, name: %s, count %lu, align %d)\n", __func__,
		(void *)cma, cma->name, count, align);

	if (!count)
		return page;

	trace_cma_alloc_start(name, count, cma->available_count, cma->count, align);

	for (r = 0; r < cma->nranges; r++) {
		page = NULL;

		ret = cma_range_alloc(cma, &cma->ranges[r], count, align,
				       &page, gfp);
		if (ret != -EBUSY || page)
			break;
	}

	/*
	 * CMA can allocate multiple page blocks, which results in different
	 * blocks being marked with different tags. Reset the tags to ignore
	 * those page blocks.
	 */
	if (page) {
		for (i = 0; i < count; i++)
			page_kasan_tag_reset(page + i);
	}

	if (ret && !(gfp & __GFP_NOWARN)) {
		pr_err_ratelimited("%s: %s: alloc failed, req-size: %lu pages, ret: %d\n",
				   __func__, cma->name, count, ret);
		cma_debug_show_areas(cma);
	}

	pr_debug("%s(): returned %p\n", __func__, page);
	trace_cma_alloc_finish(name, page ? page_to_pfn(page) : 0,
			       page, count, align, ret);
	if (page) {
		count_vm_event(CMA_ALLOC_SUCCESS);
		cma_sysfs_account_success_pages(cma, count);
	} else {
		count_vm_event(CMA_ALLOC_FAIL);
		cma_sysfs_account_fail_pages(cma, count);
	}

	return page;
}

/**
 * cma_alloc() - allocate pages from contiguous area
 * @cma:   Contiguous memory region for which the allocation is performed.
 * @count: Requested number of pages.
 * @align: Requested alignment of pages (in PAGE_SIZE order).
 * @no_warn: Avoid printing message about failed allocation
 *
 * This function allocates part of contiguous memory on specific
 * contiguous memory area.
 */
struct page *cma_alloc(struct cma *cma, unsigned long count,
		       unsigned int align, bool no_warn)
{
	return __cma_alloc(cma, count, align, GFP_KERNEL | (no_warn ? __GFP_NOWARN : 0));
}

struct folio *cma_alloc_folio(struct cma *cma, int order, gfp_t gfp)
{
	struct page *page;

	if (WARN_ON(!order || !(gfp & __GFP_COMP)))
		return NULL;

	page = __cma_alloc(cma, 1 << order, order, gfp);

	return page ? page_folio(page) : NULL;
}

bool cma_pages_valid(struct cma *cma, const struct page *pages,
		     unsigned long count)
{
	unsigned long pfn, end;
	int r;
	struct cma_memrange *cmr;
	bool ret;

	if (!cma || !pages || count > cma->count)
		return false;

	pfn = page_to_pfn(pages);
	ret = false;

	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];
		end = cmr->base_pfn + cmr->count;
		if (pfn >= cmr->base_pfn && pfn < end) {
			ret = pfn + count <= end;
			break;
		}
	}

	if (!ret)
		pr_debug("%s(page %p, count %lu)\n",
				__func__, (void *)pages, count);

	return ret;
}

/**
 * cma_release() - release allocated pages
 * @cma:   Contiguous memory region for which the allocation is performed.
 * @pages: Allocated pages.
 * @count: Number of allocated pages.
 *
 * This function releases memory allocated by cma_alloc().
 * It returns false when provided pages do not belong to contiguous area and
 * true otherwise.
 */
bool cma_release(struct cma *cma, const struct page *pages,
		 unsigned long count)
{
	struct cma_memrange *cmr;
	unsigned long pfn, end_pfn;
	int r;

	pr_debug("%s(page %p, count %lu)\n", __func__, (void *)pages, count);

	if (!cma_pages_valid(cma, pages, count))
		return false;

	pfn = page_to_pfn(pages);
	end_pfn = pfn + count;

	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];
		if (pfn >= cmr->base_pfn &&
		    pfn < (cmr->base_pfn + cmr->count)) {
			VM_BUG_ON(end_pfn > cmr->base_pfn + cmr->count);
			break;
		}
	}

	if (r == cma->nranges)
		return false;

	free_contig_range(pfn, count);
	cma_clear_bitmap(cma, cmr, pfn, count);
	cma_sysfs_account_release_pages(cma, count);
	trace_cma_release(cma->name, pfn, pages, count);

	return true;
}

bool cma_free_folio(struct cma *cma, const struct folio *folio)
{
	if (WARN_ON(!folio_test_large(folio)))
		return false;

	return cma_release(cma, &folio->page, folio_nr_pages(folio));
}

int cma_for_each_area(int (*it)(struct cma *cma, void *data), void *data)
{
	int i;

	for (i = 0; i < cma_area_count; i++) {
		int ret = it(&cma_areas[i], data);

		if (ret)
			return ret;
	}

	return 0;
}

bool cma_intersects(struct cma *cma, unsigned long start, unsigned long end)
{
	int r;
	struct cma_memrange *cmr;
	unsigned long rstart, rend;

	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];

		rstart = PFN_PHYS(cmr->base_pfn);
		rend = PFN_PHYS(cmr->base_pfn + cmr->count);
		if (end < rstart)
			continue;
		if (start >= rend)
			continue;
		return true;
	}

	return false;
}

/*
 * Very basic function to reserve memory from a CMA area that has not
 * yet been activated. This is expected to be called early, when the
 * system is single-threaded, so there is no locking. The alignment
 * checking is restrictive - only pageblock-aligned areas
 * (CMA_MIN_ALIGNMENT_BYTES) may be reserved through this function.
 * This keeps things simple, and is enough for the current use case.
 *
 * The CMA bitmaps have not yet been allocated, so just start
 * reserving from the bottom up, using a PFN to keep track
 * of what has been reserved. Unreserving is not possible.
 *
 * The caller is responsible for initializing the page structures
 * in the area properly, since this just points to memblock-allocated
 * memory. The caller should subsequently use init_cma_pageblock to
 * set the migrate type and CMA stats  the pageblocks that were reserved.
 *
 * If the CMA area fails to activate later, memory obtained through
 * this interface is not handed to the page allocator, this is
 * the responsibility of the caller (e.g. like normal memblock-allocated
 * memory).
 */
void __init *cma_reserve_early(struct cma *cma, unsigned long size)
{
	int r;
	struct cma_memrange *cmr;
	unsigned long available;
	void *ret = NULL;

	if (!cma || !cma->count)
		return NULL;
	/*
	 * Can only be called early in init.
	 */
	if (test_bit(CMA_ACTIVATED, &cma->flags))
		return NULL;

	if (!IS_ALIGNED(size, CMA_MIN_ALIGNMENT_BYTES))
		return NULL;

	if (!IS_ALIGNED(size, (PAGE_SIZE << cma->order_per_bit)))
		return NULL;

	size >>= PAGE_SHIFT;

	if (size > cma->available_count)
		return NULL;

	for (r = 0; r < cma->nranges; r++) {
		cmr = &cma->ranges[r];
		available = cmr->count - (cmr->early_pfn - cmr->base_pfn);
		if (size <= available) {
			ret = phys_to_virt(PFN_PHYS(cmr->early_pfn));
			cmr->early_pfn += size;
			cma->available_count -= size;
			return ret;
		}
	}

	return ret;
}
