/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

#include <linux/atomic.h>
#include <linux/huge_mm.h>
#include <linux/mm_types.h>
#include <linux/swap.h>
#include <linux/string.h>
#include <linux/userfaultfd_k.h>
#include <linux/swapops.h>

/**
 * folio_is_file_lru - Should the folio be on a file LRU or anon LRU?
 * @folio: The folio to test.
 *
 * We would like to get this info without a page flag, but the state
 * needs to survive until the folio is last deleted from the LRU, which
 * could be as far down as __page_cache_release.
 *
 * Return: An integer (not a boolean!) used to sort a folio onto the
 * right LRU list and to account folios correctly.
 * 1 if @folio is a regular filesystem backed page cache folio
 * or a lazily freed anonymous folio (e.g. via MADV_FREE).
 * 0 if @folio is a normal anonymous folio, a tmpfs folio or otherwise
 * ram or swap backed folio.
 */
static inline int folio_is_file_lru(const struct folio *folio)
{
	return !folio_test_swapbacked(folio);
}

static inline int page_is_file_lru(struct page *page)
{
	return folio_is_file_lru(page_folio(page));
}

static __always_inline void __update_lru_size(struct lruvec *lruvec,
				enum lru_list lru, enum zone_type zid,
				long nr_pages)
{
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);

	lockdep_assert_held(&lruvec->lru_lock);
	WARN_ON_ONCE(nr_pages != (int)nr_pages);

	__mod_lruvec_state(lruvec, NR_LRU_BASE + lru, nr_pages);
	__mod_zone_page_state(&pgdat->node_zones[zid],
				NR_ZONE_LRU_BASE + lru, nr_pages);
}

static __always_inline void update_lru_size(struct lruvec *lruvec,
				enum lru_list lru, enum zone_type zid,
				long nr_pages)
{
	__update_lru_size(lruvec, lru, zid, nr_pages);
#ifdef CONFIG_MEMCG
	mem_cgroup_update_lru_size(lruvec, lru, zid, nr_pages);
#endif
}

/**
 * __folio_clear_lru_flags - Clear page lru flags before releasing a page.
 * @folio: The folio that was on lru and now has a zero reference.
 */
static __always_inline void __folio_clear_lru_flags(struct folio *folio)
{
	VM_BUG_ON_FOLIO(!folio_test_lru(folio), folio);

	__folio_clear_lru(folio);

	/* this shouldn't happen, so leave the flags to bad_page() */
	if (folio_test_active(folio) && folio_test_unevictable(folio))
		return;

	__folio_clear_active(folio);
	__folio_clear_unevictable(folio);
}

/**
 * folio_lru_list - Which LRU list should a folio be on?
 * @folio: The folio to test.
 *
 * Return: The LRU list a folio should be on, as an index
 * into the array of LRU lists.
 */
static __always_inline enum lru_list folio_lru_list(const struct folio *folio)
{
	enum lru_list lru;

	VM_BUG_ON_FOLIO(folio_test_active(folio) && folio_test_unevictable(folio), folio);

	if (folio_test_unevictable(folio))
		return LRU_UNEVICTABLE;

	lru = folio_is_file_lru(folio) ? LRU_INACTIVE_FILE : LRU_INACTIVE_ANON;
	if (folio_test_active(folio))
		lru += LRU_ACTIVE;

	return lru;
}

#ifdef CONFIG_LRU_GEN

#ifdef CONFIG_LRU_GEN_ENABLED
static inline bool lru_gen_enabled(void)
{
	DECLARE_STATIC_KEY_TRUE(lru_gen_caps[NR_LRU_GEN_CAPS]);

	return static_branch_likely(&lru_gen_caps[LRU_GEN_CORE]);
}
#else
static inline bool lru_gen_enabled(void)
{
	DECLARE_STATIC_KEY_FALSE(lru_gen_caps[NR_LRU_GEN_CAPS]);

	return static_branch_unlikely(&lru_gen_caps[LRU_GEN_CORE]);
}
#endif

static inline bool lru_gen_in_fault(void)
{
	return current->in_lru_fault;
}

static inline int lru_gen_from_seq(unsigned long seq)
{
	return seq % MAX_NR_GENS;
}

static inline int lru_hist_from_seq(unsigned long seq)
{
	return seq % NR_HIST_GENS;
}

static inline int lru_tier_from_refs(int refs, bool workingset)
{
	VM_WARN_ON_ONCE(refs > BIT(LRU_REFS_WIDTH));

	/* see the comment on MAX_NR_TIERS */
	return workingset ? MAX_NR_TIERS - 1 : order_base_2(refs);
}

static inline int folio_lru_refs(const struct folio *folio)
{
	unsigned long flags = READ_ONCE(folio->flags.f);

	if (!(flags & BIT(PG_referenced)))
		return 0;
	/*
	 * Return the total number of accesses including PG_referenced. Also see
	 * the comment on LRU_REFS_FLAGS.
	 */
	return ((flags & LRU_REFS_MASK) >> LRU_REFS_PGOFF) + 1;
}

static inline int folio_lru_gen(const struct folio *folio)
{
	unsigned long flags = READ_ONCE(folio->flags.f);

	return ((flags & LRU_GEN_MASK) >> LRU_GEN_PGOFF) - 1;
}

static inline bool lru_gen_is_active(const struct lruvec *lruvec, int gen)
{
	unsigned long max_seq = lruvec->lrugen.max_seq;

	VM_WARN_ON_ONCE(gen >= MAX_NR_GENS);

	/* see the comment on MIN_NR_GENS */
	return gen == lru_gen_from_seq(max_seq) || gen == lru_gen_from_seq(max_seq - 1);
}

static inline void lru_gen_update_size(struct lruvec *lruvec, struct folio *folio,
				       int old_gen, int new_gen)
{
	int type = folio_is_file_lru(folio);
	int zone = folio_zonenum(folio);
	int delta = folio_nr_pages(folio);
	enum lru_list lru = type * LRU_INACTIVE_FILE;
	struct lru_gen_folio *lrugen = &lruvec->lrugen;

	VM_WARN_ON_ONCE(old_gen != -1 && old_gen >= MAX_NR_GENS);
	VM_WARN_ON_ONCE(new_gen != -1 && new_gen >= MAX_NR_GENS);
	VM_WARN_ON_ONCE(old_gen == -1 && new_gen == -1);

	if (old_gen >= 0)
		WRITE_ONCE(lrugen->nr_pages[old_gen][type][zone],
			   lrugen->nr_pages[old_gen][type][zone] - delta);
	if (new_gen >= 0)
		WRITE_ONCE(lrugen->nr_pages[new_gen][type][zone],
			   lrugen->nr_pages[new_gen][type][zone] + delta);

	/* addition */
	if (old_gen < 0) {
		if (lru_gen_is_active(lruvec, new_gen))
			lru += LRU_ACTIVE;
		__update_lru_size(lruvec, lru, zone, delta);
		return;
	}

	/* deletion */
	if (new_gen < 0) {
		if (lru_gen_is_active(lruvec, old_gen))
			lru += LRU_ACTIVE;
		__update_lru_size(lruvec, lru, zone, -delta);
		return;
	}

	/* promotion */
	if (!lru_gen_is_active(lruvec, old_gen) && lru_gen_is_active(lruvec, new_gen)) {
		__update_lru_size(lruvec, lru, zone, -delta);
		__update_lru_size(lruvec, lru + LRU_ACTIVE, zone, delta);
	}

	/* demotion requires isolation, e.g., lru_deactivate_fn() */
	VM_WARN_ON_ONCE(lru_gen_is_active(lruvec, old_gen) && !lru_gen_is_active(lruvec, new_gen));
}

static inline unsigned long lru_gen_folio_seq(const struct lruvec *lruvec,
					      const struct folio *folio,
					      bool reclaiming)
{
	int gen;
	int type = folio_is_file_lru(folio);
	const struct lru_gen_folio *lrugen = &lruvec->lrugen;

	/*
	 * +-----------------------------------+-----------------------------------+
	 * | Accessed through page tables and  | Accessed through file descriptors |
	 * | promoted by folio_update_gen()    | and protected by folio_inc_gen()  |
	 * +-----------------------------------+-----------------------------------+
	 * | PG_active (set while isolated)    |                                   |
	 * +-----------------+-----------------+-----------------+-----------------+
	 * |  PG_workingset  |  PG_referenced  |  PG_workingset  |  LRU_REFS_FLAGS |
	 * +-----------------------------------+-----------------------------------+
	 * |<---------- MIN_NR_GENS ---------->|                                   |
	 * |<---------------------------- MAX_NR_GENS ---------------------------->|
	 */
	if (folio_test_active(folio))
		gen = MIN_NR_GENS - folio_test_workingset(folio);
	else if (reclaiming)
		gen = MAX_NR_GENS;
	else if ((!folio_is_file_lru(folio) && !folio_test_swapcache(folio)) ||
		 (folio_test_reclaim(folio) &&
		  (folio_test_dirty(folio) || folio_test_writeback(folio))))
		gen = MIN_NR_GENS;
	else
		gen = MAX_NR_GENS - folio_test_workingset(folio);

	return max(READ_ONCE(lrugen->max_seq) - gen + 1, READ_ONCE(lrugen->min_seq[type]));
}

static inline bool lru_gen_add_folio(struct lruvec *lruvec, struct folio *folio, bool reclaiming)
{
	unsigned long seq;
	unsigned long flags;
	int gen = folio_lru_gen(folio);
	int type = folio_is_file_lru(folio);
	int zone = folio_zonenum(folio);
	struct lru_gen_folio *lrugen = &lruvec->lrugen;

	VM_WARN_ON_ONCE_FOLIO(gen != -1, folio);

	if (folio_test_unevictable(folio) || !lrugen->enabled)
		return false;

	seq = lru_gen_folio_seq(lruvec, folio, reclaiming);
	gen = lru_gen_from_seq(seq);
	flags = (gen + 1UL) << LRU_GEN_PGOFF;
	/* see the comment on MIN_NR_GENS about PG_active */
	set_mask_bits(&folio->flags.f, LRU_GEN_MASK | BIT(PG_active), flags);

	lru_gen_update_size(lruvec, folio, -1, gen);
	/* for folio_rotate_reclaimable() */
	if (reclaiming)
		list_add_tail(&folio->lru, &lrugen->folios[gen][type][zone]);
	else
		list_add(&folio->lru, &lrugen->folios[gen][type][zone]);

	return true;
}

static inline bool lru_gen_del_folio(struct lruvec *lruvec, struct folio *folio, bool reclaiming)
{
	unsigned long flags;
	int gen = folio_lru_gen(folio);

	if (gen < 0)
		return false;

	VM_WARN_ON_ONCE_FOLIO(folio_test_active(folio), folio);
	VM_WARN_ON_ONCE_FOLIO(folio_test_unevictable(folio), folio);

	/* for folio_migrate_flags() */
	flags = !reclaiming && lru_gen_is_active(lruvec, gen) ? BIT(PG_active) : 0;
	flags = set_mask_bits(&folio->flags.f, LRU_GEN_MASK, flags);
	gen = ((flags & LRU_GEN_MASK) >> LRU_GEN_PGOFF) - 1;

	lru_gen_update_size(lruvec, folio, gen, -1);
	list_del(&folio->lru);

	return true;
}

static inline void folio_migrate_refs(struct folio *new, const struct folio *old)
{
	unsigned long refs = READ_ONCE(old->flags.f) & LRU_REFS_MASK;

	set_mask_bits(&new->flags.f, LRU_REFS_MASK, refs);
}
#else /* !CONFIG_LRU_GEN */

static inline bool lru_gen_enabled(void)
{
	return false;
}

static inline bool lru_gen_in_fault(void)
{
	return false;
}

static inline bool lru_gen_add_folio(struct lruvec *lruvec, struct folio *folio, bool reclaiming)
{
	return false;
}

static inline bool lru_gen_del_folio(struct lruvec *lruvec, struct folio *folio, bool reclaiming)
{
	return false;
}

static inline void folio_migrate_refs(struct folio *new, const struct folio *old)
{

}
#endif /* CONFIG_LRU_GEN */

static __always_inline
void lruvec_add_folio(struct lruvec *lruvec, struct folio *folio)
{
	enum lru_list lru = folio_lru_list(folio);

	if (lru_gen_add_folio(lruvec, folio, false))
		return;

	update_lru_size(lruvec, lru, folio_zonenum(folio),
			folio_nr_pages(folio));
	if (lru != LRU_UNEVICTABLE)
		list_add(&folio->lru, &lruvec->lists[lru]);
}

static __always_inline
void lruvec_add_folio_tail(struct lruvec *lruvec, struct folio *folio)
{
	enum lru_list lru = folio_lru_list(folio);

	if (lru_gen_add_folio(lruvec, folio, true))
		return;

	update_lru_size(lruvec, lru, folio_zonenum(folio),
			folio_nr_pages(folio));
	/* This is not expected to be used on LRU_UNEVICTABLE */
	list_add_tail(&folio->lru, &lruvec->lists[lru]);
}

static __always_inline
void lruvec_del_folio(struct lruvec *lruvec, struct folio *folio)
{
	enum lru_list lru = folio_lru_list(folio);

	if (lru_gen_del_folio(lruvec, folio, false))
		return;

	if (lru != LRU_UNEVICTABLE)
		list_del(&folio->lru);
	update_lru_size(lruvec, lru, folio_zonenum(folio),
			-folio_nr_pages(folio));
}

#ifdef CONFIG_ANON_VMA_NAME
/* mmap_lock should be read-locked */
static inline void anon_vma_name_get(struct anon_vma_name *anon_name)
{
	if (anon_name)
		kref_get(&anon_name->kref);
}

static inline void anon_vma_name_put(struct anon_vma_name *anon_name)
{
	if (anon_name)
		kref_put(&anon_name->kref, anon_vma_name_free);
}

static inline
struct anon_vma_name *anon_vma_name_reuse(struct anon_vma_name *anon_name)
{
	/* Prevent anon_name refcount saturation early on */
	if (kref_read(&anon_name->kref) < REFCOUNT_MAX) {
		anon_vma_name_get(anon_name);
		return anon_name;

	}
	return anon_vma_name_alloc(anon_name->name);
}

static inline void dup_anon_vma_name(struct vm_area_struct *orig_vma,
				     struct vm_area_struct *new_vma)
{
	struct anon_vma_name *anon_name = anon_vma_name(orig_vma);

	if (anon_name)
		new_vma->anon_name = anon_vma_name_reuse(anon_name);
}

static inline void free_anon_vma_name(struct vm_area_struct *vma)
{
	/*
	 * Not using anon_vma_name because it generates a warning if mmap_lock
	 * is not held, which might be the case here.
	 */
	anon_vma_name_put(vma->anon_name);
}

static inline bool anon_vma_name_eq(struct anon_vma_name *anon_name1,
				    struct anon_vma_name *anon_name2)
{
	if (anon_name1 == anon_name2)
		return true;

	return anon_name1 && anon_name2 &&
		!strcmp(anon_name1->name, anon_name2->name);
}

#else /* CONFIG_ANON_VMA_NAME */
static inline void anon_vma_name_get(struct anon_vma_name *anon_name) {}
static inline void anon_vma_name_put(struct anon_vma_name *anon_name) {}
static inline void dup_anon_vma_name(struct vm_area_struct *orig_vma,
				     struct vm_area_struct *new_vma) {}
static inline void free_anon_vma_name(struct vm_area_struct *vma) {}

static inline bool anon_vma_name_eq(struct anon_vma_name *anon_name1,
				    struct anon_vma_name *anon_name2)
{
	return true;
}

#endif  /* CONFIG_ANON_VMA_NAME */

void pfnmap_track_ctx_release(struct kref *ref);

static inline void init_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_set(&mm->tlb_flush_pending, 0);
}

static inline void inc_tlb_flush_pending(struct mm_struct *mm)
{
	atomic_inc(&mm->tlb_flush_pending);
	/*
	 * The only time this value is relevant is when there are indeed pages
	 * to flush. And we'll only flush pages after changing them, which
	 * requires the PTL.
	 *
	 * So the ordering here is:
	 *
	 *	atomic_inc(&mm->tlb_flush_pending);
	 *	spin_lock(&ptl);
	 *	...
	 *	set_pte_at();
	 *	spin_unlock(&ptl);
	 *
	 *				spin_lock(&ptl)
	 *				mm_tlb_flush_pending();
	 *				....
	 *				spin_unlock(&ptl);
	 *
	 *	flush_tlb_range();
	 *	atomic_dec(&mm->tlb_flush_pending);
	 *
	 * Where the increment if constrained by the PTL unlock, it thus
	 * ensures that the increment is visible if the PTE modification is
	 * visible. After all, if there is no PTE modification, nobody cares
	 * about TLB flushes either.
	 *
	 * This very much relies on users (mm_tlb_flush_pending() and
	 * mm_tlb_flush_nested()) only caring about _specific_ PTEs (and
	 * therefore specific PTLs), because with SPLIT_PTE_PTLOCKS and RCpc
	 * locks (PPC) the unlock of one doesn't order against the lock of
	 * another PTL.
	 *
	 * The decrement is ordered by the flush_tlb_range(), such that
	 * mm_tlb_flush_pending() will not return false unless all flushes have
	 * completed.
	 */
}

static inline void dec_tlb_flush_pending(struct mm_struct *mm)
{
	/*
	 * See inc_tlb_flush_pending().
	 *
	 * This cannot be smp_mb__before_atomic() because smp_mb() simply does
	 * not order against TLB invalidate completion, which is what we need.
	 *
	 * Therefore we must rely on tlb_flush_*() to guarantee order.
	 */
	atomic_dec(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_pending(const struct mm_struct *mm)
{
	/*
	 * Must be called after having acquired the PTL; orders against that
	 * PTLs release and therefore ensures that if we observe the modified
	 * PTE we must also observe the increment from inc_tlb_flush_pending().
	 *
	 * That is, it only guarantees to return true if there is a flush
	 * pending for _this_ PTL.
	 */
	return atomic_read(&mm->tlb_flush_pending);
}

static inline bool mm_tlb_flush_nested(const struct mm_struct *mm)
{
	/*
	 * Similar to mm_tlb_flush_pending(), we must have acquired the PTL
	 * for which there is a TLB flush pending in order to guarantee
	 * we've seen both that PTE modification and the increment.
	 *
	 * (no requirement on actually still holding the PTL, that is irrelevant)
	 */
	return atomic_read(&mm->tlb_flush_pending) > 1;
}

#ifdef CONFIG_MMU
/*
 * Computes the pte marker to copy from the given source entry into dst_vma.
 * If no marker should be copied, returns 0.
 * The caller should insert a new pte created with make_pte_marker().
 */
static inline pte_marker copy_pte_marker(
		swp_entry_t entry, struct vm_area_struct *dst_vma)
{
	pte_marker srcm = pte_marker_get(entry);
	/* Always copy error entries. */
	pte_marker dstm = srcm & (PTE_MARKER_POISONED | PTE_MARKER_GUARD);

	/* Only copy PTE markers if UFFD register matches. */
	if ((srcm & PTE_MARKER_UFFD_WP) && userfaultfd_wp(dst_vma))
		dstm |= PTE_MARKER_UFFD_WP;

	return dstm;
}
#endif

/*
 * If this pte is wr-protected by uffd-wp in any form, arm the special pte to
 * replace a none pte.  NOTE!  This should only be called when *pte is already
 * cleared so we will never accidentally replace something valuable.  Meanwhile
 * none pte also means we are not demoting the pte so tlb flushed is not needed.
 * E.g., when pte cleared the caller should have taken care of the tlb flush.
 *
 * Must be called with pgtable lock held so that no thread will see the none
 * pte, and if they see it, they'll fault and serialize at the pgtable lock.
 *
 * Returns true if an uffd-wp pte was installed, false otherwise.
 */
static inline bool
pte_install_uffd_wp_if_needed(struct vm_area_struct *vma, unsigned long addr,
			      pte_t *pte, pte_t pteval)
{
#ifdef CONFIG_PTE_MARKER_UFFD_WP
	bool arm_uffd_pte = false;

	/* The current status of the pte should be "cleared" before calling */
	WARN_ON_ONCE(!pte_none(ptep_get(pte)));

	/*
	 * NOTE: userfaultfd_wp_unpopulated() doesn't need this whole
	 * thing, because when zapping either it means it's dropping the
	 * page, or in TTU where the present pte will be quickly replaced
	 * with a swap pte.  There's no way of leaking the bit.
	 */
	if (vma_is_anonymous(vma) || !userfaultfd_wp(vma))
		return false;

	/* A uffd-wp wr-protected normal pte */
	if (unlikely(pte_present(pteval) && pte_uffd_wp(pteval)))
		arm_uffd_pte = true;

	/*
	 * A uffd-wp wr-protected swap pte.  Note: this should even cover an
	 * existing pte marker with uffd-wp bit set.
	 */
	if (unlikely(pte_swp_uffd_wp_any(pteval)))
		arm_uffd_pte = true;

	if (unlikely(arm_uffd_pte)) {
		set_pte_at(vma->vm_mm, addr, pte,
			   make_pte_marker(PTE_MARKER_UFFD_WP));
		return true;
	}
#endif
	return false;
}

static inline bool vma_has_recency(const struct vm_area_struct *vma)
{
	if (vma->vm_flags & (VM_SEQ_READ | VM_RAND_READ))
		return false;

	if (vma->vm_file && (vma->vm_file->f_mode & FMODE_NOREUSE))
		return false;

	return true;
}

/**
 * num_pages_contiguous() - determine the number of contiguous pages
 *			    that represent contiguous PFNs
 * @pages: an array of page pointers
 * @nr_pages: length of the array, at least 1
 *
 * Determine the number of contiguous pages that represent contiguous PFNs
 * in @pages, starting from the first page.
 *
 * In some kernel configs contiguous PFNs will not have contiguous struct
 * pages. In these configurations num_pages_contiguous() will return a num
 * smaller than ideal number. The caller should continue to check for pfn
 * contiguity after each call to num_pages_contiguous().
 *
 * Returns the number of contiguous pages.
 */
static inline size_t num_pages_contiguous(struct page **pages, size_t nr_pages)
{
	struct page *cur_page = pages[0];
	unsigned long section = memdesc_section(cur_page->flags);
	size_t i;

	for (i = 1; i < nr_pages; i++) {
		if (++cur_page != pages[i])
			break;
		/*
		 * In unproblematic kernel configs, page_to_section() == 0 and
		 * the whole check will get optimized out.
		 */
		if (memdesc_section(cur_page->flags) != section)
			break;
	}

	return i;
}

#endif
