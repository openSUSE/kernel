#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/swap.h> /* for totalram_pages */

void *kmap(struct page *page)
{
	if (!PageHighMem(page))
		return page_address(page);
	might_sleep();
	return kmap_high(page);
}

void kunmap(struct page *page)
{
	if (in_interrupt())
		BUG();
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}

struct page *kmap_to_page(void *ptr)
{
	struct page *page;

	if ((unsigned long)ptr < PKMAP_ADDR(0))
		return virt_to_page(ptr);
	page = pte_page(pkmap_page_table[PKMAP_NR((unsigned long)ptr)]);
	return page;
}
EXPORT_SYMBOL_GPL(kmap_to_page); /* PREEMPT_RT converts some modules to use this */

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap because
 * no global lock is needed and because the kmap code must perform a global TLB
 * invalidation when the kmap pool wraps.
 *
 * However when holding an atomic kmap it is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 */
void *__kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	/* even !CONFIG_PREEMPT needs this, for in_atomic in do_page_fault */
	preempt_disable();
	pagefault_disable();

	if (!PageHighMem(page))
		return page_address(page);

	debug_kmap_atomic(type);

	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	WARN_ON(!pte_none(*(kmap_pte-idx)));
	set_pte(kmap_pte-idx, mk_pte(page, prot));

	return (void *)vaddr;
}

void *__kmap_atomic_direct(struct page *page, enum km_type type)
{
	return __kmap_atomic_prot(page, type, kmap_prot);
}

void *__kmap_atomic(struct page *page, enum km_type type)
{
	return kmap_atomic_prot(page, type, kmap_prot);
}

/*
 * This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *__kmap_atomic_pfn(unsigned long pfn, enum km_type type)
{
	preempt_disable();
	return kmap_atomic_prot_pfn(pfn, type, kmap_prot);
}
EXPORT_SYMBOL_GPL(__kmap_atomic_pfn); /* temporarily in use by i915 GEM until vmap */

struct page *__kmap_atomic_to_page(void *ptr)
{
	unsigned long idx, vaddr = (unsigned long)ptr;
	pte_t *pte;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	idx = virt_to_fix(vaddr);
	pte = kmap_pte - (idx - FIX_KMAP_BEGIN);
	return pte_page(*pte);
}

EXPORT_SYMBOL(kmap);
EXPORT_SYMBOL(kunmap);
EXPORT_SYMBOL(kunmap_virt);
EXPORT_SYMBOL(__kmap_atomic);
EXPORT_SYMBOL(__kmap_atomic_prot);
EXPORT_SYMBOL(__kmap_atomic_to_page);

void __init set_highmem_pages_init(void)
{
	struct zone *zone;
	int nid;

	for_each_zone(zone) {
		unsigned long zone_start_pfn, zone_end_pfn;

		if (!is_highmem(zone))
			continue;

		zone_start_pfn = zone->zone_start_pfn;
		zone_end_pfn = zone_start_pfn + zone->spanned_pages;

		nid = zone_to_nid(zone);
		printk(KERN_INFO "Initializing %s for node %d (%08lx:%08lx)\n",
				zone->name, nid, zone_start_pfn, zone_end_pfn);

		add_highpages_with_active_regions(nid, zone_start_pfn,
				 zone_end_pfn);
	}
	totalram_pages += totalhigh_pages;
}
