/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#ifndef _ASM_X86_HIGHMEM_H
#define _ASM_X86_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/interrupt.h>
#include <linux/threads.h>
#include <asm/kmap_types.h>
#include <asm/tlbflush.h>
#include <asm/paravirt.h>
#include <asm/fixmap.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
/*
 * Ordering is:
 *
 * FIXADDR_TOP
 * 			fixed_addresses
 * FIXADDR_START
 * 			temp fixed addresses
 * FIXADDR_BOOT_START
 * 			Persistent kmap area
 * PKMAP_BASE
 * VMALLOC_END
 * 			Vmalloc area
 * VMALLOC_START
 * high_memory
 */
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern void *kmap_high(struct page *page);
extern void *kmap_pfn_prot(unsigned long pfn, pgprot_t prot);
extern void kunmap_high(struct page *page);

void *kmap(struct page *page);
void *kmap_page_prot(struct page *page, pgprot_t prot);
extern void kunmap_virt(void *ptr);
extern struct page *kmap_to_page(void *ptr);
void kunmap(struct page *page);

void *__kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot);
void *__kmap_atomic_prot_pfn(unsigned long pfn, enum km_type type, pgprot_t prot);
void *__kmap_atomic(struct page *page, enum km_type type);
void *__kmap_atomic_direct(struct page *page, enum km_type type);
void __kunmap_atomic(void *kvaddr, enum km_type type);
void *__kmap_atomic_pfn(unsigned long pfn, enum km_type type);
struct page *__kmap_atomic_to_page(void *ptr);

#ifndef CONFIG_PARAVIRT
#define kmap_atomic_pte(page, type)		kmap_atomic(page, type)
#define kmap_atomic_pte_direct(page, type)	kmap_atomic_direct(page, type)
#endif

#define flush_cache_kmaps()	do { } while (0)

extern void add_highpages_with_active_regions(int nid, unsigned long start_pfn,
					unsigned long end_pfn);

/*
 * on PREEMPT_RT kmap_atomic() is a wrapper that uses kmap():
 */
#ifdef CONFIG_PREEMPT_RT
# define kmap_atomic_prot(page, type, prot)	({ pagefault_disable(); kmap_pfn_prot(page_to_pfn(page), prot); })
# define kmap_atomic_prot_pfn(pfn, type, prot)	({ pagefault_disable(); kmap_pfn_prot(pfn, prot); })
# define kmap_atomic(page, type)	({ pagefault_disable(); kmap(page); })
# define kmap_atomic_pfn(pfn, type)	kmap(pfn_to_page(pfn))
# define kunmap_atomic(kvaddr, type)	do { kunmap_virt(kvaddr); pagefault_enable(); } while(0)
# define kmap_atomic_to_page(kvaddr)	kmap_to_page(kvaddr)
# define kmap_atomic_direct(page, type)	__kmap_atomic_direct(page, type)
# define kunmap_atomic_direct(kvaddr, type)	__kunmap_atomic(kvaddr, type)
#else
# define kmap_atomic_prot(page, type, prot)	__kmap_atomic_prot(page, type, prot)
# define kmap_atomic_prot_pfn(pfn, type, prot)  __kmap_atomic_prot_pfn(pfn, type, prot)
# define kmap_atomic(page, type)	__kmap_atomic(page, type)
# define kmap_atomic_pfn(pfn, type)	__kmap_atomic_pfn(pfn, type)
# define kunmap_atomic(kvaddr, type)	__kunmap_atomic(kvaddr, type)
# define kmap_atomic_to_page(kvaddr)	__kmap_atomic_to_page(kvaddr)
# define kmap_atomic_direct(page, type)	__kmap_atomic(page, type)
# define kunmap_atomic_direct(kvaddr, type)	__kunmap_atomic(kvaddr, type)
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_HIGHMEM_H */
