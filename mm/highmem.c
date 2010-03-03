/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 *
 * Largely rewritten to get rid of all global locks
 *
 * Copyright (C) 2006 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/hardirq.h>

#include <asm/tlbflush.h>
#include <asm/pgtable.h>

#ifdef CONFIG_HIGHMEM

static int __set_page_address(struct page *page, void *virtual, int pos);

unsigned long totalhigh_pages __read_mostly;
EXPORT_SYMBOL(totalhigh_pages);

unsigned int nr_free_highpages (void)
{
	pg_data_t *pgdat;
	unsigned int pages = 0;

	for_each_online_pgdat(pgdat) {
		pages += zone_page_state(&pgdat->node_zones[ZONE_HIGHMEM],
			NR_FREE_PAGES);
		if (zone_movable_is_highmem())
			pages += zone_page_state(
					&pgdat->node_zones[ZONE_MOVABLE],
					NR_FREE_PAGES);
	}

	return pages;
}

/*
 * count is not a pure "count".
 *  0 means its owned exclusively by someone
 *  1 means its free for use - either mapped or not.
 *  n means that there are (n-1) current users of it.
 */

struct pkmap_state {
	atomic_t count;
	int	 pfn;
};

static struct pkmap_state pkmap[LAST_PKMAP];
static atomic_t pkmap_hand;
static atomic_t pkmap_free;
static atomic_t pkmap_users;

pte_t * pkmap_page_table;

static DECLARE_WAIT_QUEUE_HEAD(pkmap_wait);


/*
 * Most architectures have no use for kmap_high_get(), so let's abstract
 * the disabling of IRQ out of the locking in that case to save on a
 * potential useless overhead.
 */
#ifdef ARCH_NEEDS_KMAP_HIGH_GET
#define lock_kmap()             spin_lock_irq(&kmap_lock)
#define unlock_kmap()           spin_unlock_irq(&kmap_lock)
#define lock_kmap_any(flags)    spin_lock_irqsave(&kmap_lock, flags)
#define unlock_kmap_any(flags)  spin_unlock_irqrestore(&kmap_lock, flags)
#else
#define lock_kmap()             spin_lock(&kmap_lock)
#define unlock_kmap()           spin_unlock(&kmap_lock)
#define lock_kmap_any(flags)    \
		do { spin_lock(&kmap_lock); (void)(flags); } while (0)
#define unlock_kmap_any(flags)  \
		do { spin_unlock(&kmap_lock); (void)(flags); } while (0)
#endif

/*
 * Try to free a given kmap slot.
 *
 * Returns:
 *  -1 - in use
 *   0 - free, no TLB flush needed
 *   1 - free, needs TLB flush
 */
static int pkmap_try_free(int pos)
{
	if (atomic_cmpxchg(&pkmap[pos].count, 1, 0) != 1)
		return -1;
	atomic_dec(&pkmap_free);
	/*
	 * TODO: add a young bit to make it CLOCK
	 */
	if (!pte_none(pkmap_page_table[pos])) {
		unsigned long addr = PKMAP_ADDR(pos);
		pte_t *ptep = &pkmap_page_table[pos];

		if (!pkmap[pos].pfn) {
			struct page *page = pte_page(pkmap_page_table[pos]);
			VM_BUG_ON(addr != (unsigned long)page_address(page));
			if (!__set_page_address(page, NULL, pos))
				BUG();
			flush_kernel_dcache_page(page);
		}

		pte_clear(&init_mm, addr, ptep);

		return 1;
	}

	return 0;
}

static inline void pkmap_put(atomic_t *counter)
{
	switch (atomic_dec_return(counter)) {
	case 0:
		BUG();

	case 1:
		atomic_inc(&pkmap_free);
		wake_up(&pkmap_wait);
	}
}

#define TLB_BATCH	32

static int pkmap_get_free(void)
{
	int i, pos, flush;

restart:
	for (i = 0; i < LAST_PKMAP; i++) {
		pos = atomic_inc_return(&pkmap_hand) & LAST_PKMAP_MASK;
		flush = pkmap_try_free(pos);
		if (flush >= 0)
			goto got_one;
	}

	atomic_dec(&pkmap_free);
	/*
	 * wait for somebody else to unmap their entries
	 */
	if (likely(!in_interrupt()))
		wait_event(pkmap_wait, atomic_read(&pkmap_free) != 0);

	goto restart;

got_one:
	if (flush) {
#if 0
		flush_tlb_kernel_range(PKMAP_ADDR(pos), PKMAP_ADDR(pos+1));
#else
		int pos2 = (pos + 1) & LAST_PKMAP_MASK;
		int nr;
		int entries[TLB_BATCH];

		/*
		 * For those architectures that cannot help but flush the
		 * whole TLB, flush some more entries to make it worthwhile.
		 * Scan ahead of the hand to minimise search distances.
		 */
		for (i = 0, nr = 0; i < LAST_PKMAP && nr < TLB_BATCH;
				i++, pos2 = (pos2 + 1) & LAST_PKMAP_MASK) {

			flush = pkmap_try_free(pos2);
			if (flush < 0)
				continue;

			if (!flush) {
				atomic_t *counter = &pkmap[pos2].count;
				VM_BUG_ON(atomic_read(counter) != 0);
				atomic_set(counter, 2);
				pkmap_put(counter);
			} else
				entries[nr++] = pos2;
		}
		flush_tlb_kernel_range(PKMAP_ADDR(0), PKMAP_ADDR(LAST_PKMAP));

		for (i = 0; i < nr; i++) {
			atomic_t *counter = &pkmap[entries[i]].count;
			VM_BUG_ON(atomic_read(counter) != 0);
			atomic_set(counter, 2);
			pkmap_put(counter);
		}
#endif
	}
	return pos;
}

static unsigned long pkmap_insert(unsigned long pfn, pgprot_t prot)
{
	int pos = pkmap_get_free();
	unsigned long vaddr = PKMAP_ADDR(pos);
	pte_t *ptep = &pkmap_page_table[pos];
	pte_t entry = pfn_pte(pfn, prot);
	atomic_t *counter = &pkmap[pos].count;

	VM_BUG_ON(atomic_read(counter) != 0);
	set_pte_at(&init_mm, vaddr, ptep, entry);

	pkmap[pos].pfn =
		!(pgprot_val(prot) == pgprot_val(kmap_prot) && pfn_valid(pfn));

	if (!pkmap[pos].pfn) {
		struct page *page = pfn_to_page(pfn);

		if (unlikely(!__set_page_address(page, (void *)vaddr, pos))) {
			/*
			 * concurrent pkmap_inserts for this page -
			 * the other won the race, release this entry.
			 *
			 * we can still clear the pte without a tlb flush since
			 * it couldn't have been used yet.
			 */
			pte_clear(&init_mm, vaddr, ptep);
			VM_BUG_ON(atomic_read(counter) != 0);
			atomic_set(counter, 2);
			pkmap_put(counter);
			return 0;
		}
	} else {
#ifdef ARCH_NEEDS_KMAP_HIGH_GET
		/*
		 * non-default prot and pure pfn memory doesn't
		 * get map deduplication, nor a working page_address
		 *
		 * this also makes it incompatible with
		 * ARCH_NEEDS_KMAP_HIGH_GET
		 */
		BUG();
#endif
	}

	atomic_set(counter, 2);

	return vaddr;
}

/*
 * Flush all unused kmap mappings in order to remove stray mappings.
 */
void kmap_flush_unused(void)
{
	WARN_ON_ONCE(1);
}

/*
 * Avoid starvation deadlock by limiting the number of tasks that can obtain a
 * kmap to (LAST_PKMAP - KM_TYPE_NR*NR_CPUS)/2.
 */
static void kmap_account(void)
{
	int weight;

#ifndef CONFIG_PREEMPT_RT
	if (in_interrupt()) {
		/* irqs can always get them */
		weight = -1;
	} else
#endif
	if (current->flags & PF_KMAP) {
		current->flags &= ~PF_KMAP;
		/* we already accounted the second */
		weight = 0;
	} else {
		/* mark 1, account 2 */
		current->flags |= PF_KMAP;
		weight = 2;
	}

	if (weight > 0) {
		/*
		 * reserve KM_TYPE_NR maps per CPU for interrupt context
		 */
		const int target = LAST_PKMAP
#ifndef CONFIG_PREEMPT_RT
				- KM_TYPE_NR*NR_CPUS
#endif
			;

again:
		wait_event(pkmap_wait,
			atomic_read(&pkmap_users) + weight <= target);

		if (atomic_add_return(weight, &pkmap_users) > target) {
			atomic_sub(weight, &pkmap_users);
			goto again;
		}
	}
}

static void kunmap_account(void)
{
	int weight;

#ifndef CONFIG_PREEMPT_RT
	if (in_irq()) {
		weight = -1;
	} else
#endif
	if (current->flags & PF_KMAP) {
		/* there was only 1 kmap, un-account both */
		current->flags &= ~PF_KMAP;
		weight = 2;
	} else {
		/* there were two kmaps, un-account per kunmap */
		weight = 1;
	}

	if (weight > 0)
		atomic_sub(weight, &pkmap_users);
	wake_up(&pkmap_wait);
}

void *kmap_get(struct page *page)
{
	unsigned long vaddr;
again:
	vaddr = (unsigned long)page_address(page);
	if (vaddr) {
		atomic_t *counter = &pkmap[PKMAP_NR(vaddr)].count;
		if (atomic_inc_not_zero(counter)) {
			/*
			 * atomic_inc_not_zero implies a (memory) barrier on
			 * success so page address will be reloaded.
			 */
			unsigned long vaddr2 = (unsigned long)page_address(page);
			if (likely(vaddr == vaddr2))
				return (void *)vaddr;

			/*
			 * Oops, we got someone else.
			 *
			 * This can happen if we get preempted after
			 * page_address() and before atomic_inc_not_zero()
			 * and during that preemption this slot is freed and
			 * reused.
			 */
			pkmap_put(counter);
		}
		goto again;
	}
	return (void *)vaddr;
}

void *kmap_high(struct page *page)
{
	unsigned long vaddr;

	kmap_account();

again:
	vaddr = (unsigned long)kmap_get(page);
	if (!vaddr) {
		vaddr = pkmap_insert(page_to_pfn(page), kmap_prot);
		if (!vaddr)
			goto again;
	}

	return (void *)vaddr;
}

EXPORT_SYMBOL(kmap_high);

void *kmap_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	unsigned long vaddr;

	if (pgprot_val(prot) == pgprot_val(kmap_prot) &&
			pfn_valid(pfn) && PageHighMem(pfn_to_page(pfn)))
		return kmap_high(pfn_to_page(pfn));

	kmap_account();

	vaddr = pkmap_insert(pfn, prot);
	BUG_ON(!vaddr);

	return (void *)vaddr;
}

EXPORT_SYMBOL(kmap_pfn_prot);

#ifdef ARCH_NEEDS_KMAP_HIGH_GET
/**
 * kmap_high_get - pin a highmem page into memory
 * @page: &struct page to pin
 *
 * Returns the page's current virtual memory address, or NULL if no mapping
 * exists.  When and only when a non null address is returned then a
 * matching call to kunmap_high() is necessary.
 *
 * This can be called from any context.
 */
void *kmap_high_get(struct page *page)
{
	unsigned long vaddr, flags;

	lock_kmap_any(flags);
	vaddr = (unsigned long)kmap_get(page);
	unlock_kmap_any(flags);
	return (void *)vaddr;
}
#endif

void kunmap_virt(void *ptr)
{
	unsigned long vaddr = (unsigned long)ptr;
	if (vaddr < PKMAP_ADDR(0) || vaddr >= PKMAP_ADDR(LAST_PKMAP))
		return;
	pkmap_put(&pkmap[PKMAP_NR(vaddr)].count);
	kunmap_account();
}

void kunmap_high(struct page *page)
{
	unsigned long vaddr = (unsigned long)page_address(page);
	BUG_ON(!vaddr);
	pkmap_put(&pkmap[PKMAP_NR(vaddr)].count);
	kunmap_account();
}

EXPORT_SYMBOL(kunmap_high);
#endif

#if defined(HASHED_PAGE_VIRTUAL)

#define PA_HASH_ORDER	7

/*
 * Describes one page->virtual address association.
 */
static struct page_address_map {
	struct page *page;
	void *virtual;
	struct list_head list;
} page_address_maps[LAST_PKMAP];

/*
 * Hash table bucket
 */
static struct page_address_slot {
	struct list_head lh;			/* List of page_address_maps */
	spinlock_t lock;			/* Protect this bucket's list */
} ____cacheline_aligned_in_smp page_address_htable[1<<PA_HASH_ORDER];

static struct page_address_slot *page_slot(struct page *page)
{
	return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];
}

/**
 * page_address - get the mapped virtual address of a page
 * @page: &struct page to get the virtual address of
 *
 * Returns the page's virtual address.
 */

static void *__page_address(struct page_address_slot *pas, struct page *page)
{
	void *ret = NULL;

	if (!list_empty(&pas->lh)) {
		struct page_address_map *pam;

		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				ret = pam->virtual;
				break;
			}
		}
	}

	return ret;
}

void *page_address(struct page *page)
{
	unsigned long flags;
	void *ret;
	struct page_address_slot *pas;

	if (!PageHighMem(page))
		return lowmem_page_address(page);

	pas = page_slot(page);
	spin_lock_irqsave(&pas->lock, flags);
	ret = __page_address(pas, page);
	spin_unlock_irqrestore(&pas->lock, flags);
	return ret;
}

EXPORT_SYMBOL(page_address);

/**
 * set_page_address - set a page's virtual address
 * @page: &struct page to set
 * @virtual: virtual address to use
 */
static int __set_page_address(struct page *page, void *virtual, int pos)
{
	int ret = 0;
	unsigned long flags;
	struct page_address_slot *pas;
	struct page_address_map *pam;

	VM_BUG_ON(!PageHighMem(page));
	VM_BUG_ON(atomic_read(&pkmap[pos].count) != 0);
	VM_BUG_ON(pos < 0 || pos >= LAST_PKMAP);

	pas = page_slot(page);
	pam = &page_address_maps[pos];

	spin_lock_irqsave(&pas->lock, flags);
	if (virtual) { /* add */
		VM_BUG_ON(!list_empty(&pam->list));

		if (!__page_address(pas, page)) {
			pam->page = page;
			pam->virtual = virtual;
			list_add_tail(&pam->list, &pas->lh);
			ret = 1;
		}
	} else { /* remove */
		if (!list_empty(&pam->list)) {
			list_del_init(&pam->list);
			ret = 1;
		}
	}
	spin_unlock_irqrestore(&pas->lock, flags);

	return ret;
}

int set_page_address(struct page *page, void *virtual)
{
	/*
	 * set_page_address is not supposed to be called when using
	 * hashed virtual addresses.
	 */
	BUG();
	return 0;
}

void __init __page_address_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(page_address_maps); i++)
		INIT_LIST_HEAD(&page_address_maps[i].list);

	for (i = 0; i < ARRAY_SIZE(page_address_htable); i++) {
		INIT_LIST_HEAD(&page_address_htable[i].lh);
		spin_lock_init(&page_address_htable[i].lock);
	}
}

#elif defined (CONFIG_HIGHMEM) /* HASHED_PAGE_VIRTUAL */

static int __set_page_address(struct page *page, void *virtual, int pos)
{
	return set_page_address(page, virtual);
}

#endif	/* defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL) */

#if defined(CONFIG_HIGHMEM) || defined(HASHED_PAGE_VIRTUAL)

void __init page_address_init(void)
{
#ifdef CONFIG_HIGHMEM
	int i;

	for (i = 0; i < ARRAY_SIZE(pkmap); i++)
		atomic_set(&pkmap[i].count, 1);
	atomic_set(&pkmap_hand, 0);
	atomic_set(&pkmap_free, LAST_PKMAP);
	atomic_set(&pkmap_users, 0);
#endif

#ifdef HASHED_PAGE_VIRTUAL
	__page_address_init();
#endif
}

#endif	/* defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL) */

#if defined(CONFIG_DEBUG_HIGHMEM) && defined(CONFIG_TRACE_IRQFLAGS_SUPPORT)

void debug_kmap_atomic(enum km_type type)
{
	static int warn_count = 10;

	if (unlikely(warn_count < 0))
		return;

	if (unlikely(in_interrupt())) {
		if (in_nmi()) {
			if (type != KM_NMI && type != KM_NMI_PTE) {
				WARN_ON(1);
				warn_count--;
			}
		} else if (in_irq()) {
			if (type != KM_IRQ0 && type != KM_IRQ1 &&
			    type != KM_BIO_SRC_IRQ && type != KM_BIO_DST_IRQ &&
			    type != KM_BOUNCE_READ && type != KM_IRQ_PTE) {
				WARN_ON(1);
				warn_count--;
			}
		} else if (!irqs_disabled()) {	/* softirq */
			if (type != KM_IRQ0 && type != KM_IRQ1 &&
			    type != KM_SOFTIRQ0 && type != KM_SOFTIRQ1 &&
			    type != KM_SKB_SUNRPC_DATA &&
			    type != KM_SKB_DATA_SOFTIRQ &&
			    type != KM_BOUNCE_READ) {
				WARN_ON(1);
				warn_count--;
			}
		}
	}

	if (type == KM_IRQ0 || type == KM_IRQ1 || type == KM_BOUNCE_READ ||
			type == KM_BIO_SRC_IRQ || type == KM_BIO_DST_IRQ ||
			type == KM_IRQ_PTE || type == KM_NMI ||
			type == KM_NMI_PTE ) {
		if (!irqs_disabled()) {
			WARN_ON(1);
			warn_count--;
		}
	} else if (type == KM_SOFTIRQ0 || type == KM_SOFTIRQ1) {
		if (irq_count() == 0 && !irqs_disabled()) {
			WARN_ON(1);
			warn_count--;
		}
	}
}

#endif
