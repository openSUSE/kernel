/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgalloc.h"
#include "asm/tlbflush.h"
#include "choose-mode.h"
#include "mode_kern.h"
#include "tlb.h"
#include "mem.h"

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	address &= PAGE_MASK;
	flush_tlb_range(vma, address, address + PAGE_SIZE);
}

void flush_tlb_all(void)
{
	flush_tlb_mm(current->mm);
}
  
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	CHOOSE_MODE_PROC(flush_tlb_kernel_range_tt, 
			 flush_tlb_kernel_range_skas, start, end);
}

void flush_tlb_kernel_vm(void)
{
	CHOOSE_MODE(flush_tlb_kernel_vm_tt(), flush_tlb_kernel_vm_skas());
}

void __flush_tlb_one(unsigned long addr)
{
	CHOOSE_MODE_PROC(__flush_tlb_one_tt, __flush_tlb_one_skas, addr);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
	CHOOSE_MODE_PROC(flush_tlb_range_tt, flush_tlb_range_skas, vma, start, 
			 end);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	CHOOSE_MODE_PROC(flush_tlb_mm_tt, flush_tlb_mm_skas, mm);
}

void force_flush_all(void)
{
	CHOOSE_MODE(force_flush_all_tt(), force_flush_all_skas());
}

pgd_t *pgd_offset_proc(struct mm_struct *mm, unsigned long address)
{
	return(pgd_offset(mm, address));
}

pud_t *pud_offset_proc(pgd_t *pgd, unsigned long address)
{
	return(pud_offset(pgd, address));
}

pmd_t *pmd_offset_proc(pud_t *pud, unsigned long address)
{
	return(pmd_offset(pud, address));
}

pte_t *pte_offset_proc(pmd_t *pmd, unsigned long address)
{
	return(pte_offset_kernel(pmd, address));
}

pte_t *addr_pte(struct task_struct *task, unsigned long addr)
{
	pgd_t *pgd = pgd_offset(task->mm, addr);
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return(pte_offset_map(pmd, addr));
}

int add_mmap(unsigned long virt, unsigned long phys, unsigned long len,
	     int r, int w, int x, struct host_vm_op *ops, int index,
	     int last_filled, int data,
	     void (*do_ops)(int, struct host_vm_op *, int))
{
	__u64 offset;
	struct host_vm_op *last;
	int fd;

	fd = phys_mapping(phys, &offset);
	if(index != -1){
		last = &ops[index];
		if((last->type == MMAP) &&
		   (last->u.mmap.addr + last->u.mmap.len == virt) &&
		   (last->u.mmap.r == r) && (last->u.mmap.w == w) &&
		   (last->u.mmap.x == x) && (last->u.mmap.fd == fd) &&
		   (last->u.mmap.offset + last->u.mmap.len == offset)){
			last->u.mmap.len += len;
			return(index);
		}
	}

	if(index == last_filled){
		(*do_ops)(data, ops, last_filled);
		index = -1;
	}

	ops[++index] = ((struct host_vm_op) { .type	= MMAP,
					      .u = { .mmap = {
						      .addr	= virt,
						      .len	= len,
						      .r	= r,
						      .w	= w,
						      .x	= x,
						      .fd	= fd,
						      .offset	= offset }
					      } });
	return(index);
}

int add_munmap(unsigned long addr, unsigned long len, struct host_vm_op *ops,
	       int index, int last_filled, int data,
	       void (*do_ops)(int, struct host_vm_op *, int))
{
	struct host_vm_op *last;

	if(index != -1){
		last = &ops[index];
		if((last->type == MUNMAP) &&
		   (last->u.munmap.addr + last->u.mmap.len == addr)){
			last->u.munmap.len += len;
			return(index);
		}
	}

	if(index == last_filled){
		(*do_ops)(data, ops, last_filled);
		index = -1;
	}

	ops[++index] = ((struct host_vm_op) { .type	= MUNMAP,
					      .u = { .munmap = {
						      .addr	= addr,
						      .len	= len } } });
	return(index);
}

int add_mprotect(unsigned long addr, unsigned long len, int r, int w, int x,
		 struct host_vm_op *ops, int index, int last_filled, int data,
		 void (*do_ops)(int, struct host_vm_op *, int))
{
	struct host_vm_op *last;

	if(index != -1){
		last = &ops[index];
		if((last->type == MPROTECT) &&
		   (last->u.mprotect.addr + last->u.mprotect.len == addr) &&
		   (last->u.mprotect.r == r) && (last->u.mprotect.w == w) &&
		   (last->u.mprotect.x == x)){
			last->u.mprotect.len += len;
			return(index);
		}
	}

	if(index == last_filled){
		(*do_ops)(data, ops, last_filled);
		index = -1;
	}

	ops[++index] = ((struct host_vm_op) { .type	= MPROTECT,
					      .u = { .mprotect = {
						      .addr	= addr,
						      .len	= len,
						      .r	= r,
						      .w	= w,
						      .x	= x } } });
	return(index);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
