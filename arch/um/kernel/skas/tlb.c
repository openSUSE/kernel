/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/mmu.h"
#include "user_util.h"
#include "mem_user.h"
#include "mem.h"
#include "skas.h"
#include "os.h"
#include "tlb.h"

static void do_ops(int fd, struct host_vm_op *ops, int last)
{
	struct host_vm_op *op;
	int i;

	for(i = 0; i <= last; i++){
		op = &ops[i];
		switch(op->type){
		case MMAP:
			map(fd, op->u.mmap.addr, op->u.mmap.len,
			    op->u.mmap.r, op->u.mmap.w, op->u.mmap.x,
			    op->u.mmap.fd, op->u.mmap.offset);
			break;
		case MUNMAP:
			unmap(fd, (void *) op->u.munmap.addr,
			      op->u.munmap.len);
			break;
		case MPROTECT:
			protect(fd, op->u.mprotect.addr, op->u.mprotect.len,
				op->u.mprotect.r, op->u.mprotect.w,
				op->u.mprotect.x);
			break;
		default:
			printk("Unknown op type %d in do_ops\n", op->type);
			break;
		}
	}
}

static void fix_range(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
	pgd_t *npgd;
	pud_t *npud;
	pmd_t *npmd;
	pte_t *npte;
	unsigned long addr,  end;
	int r, w, x, fd;
	struct host_vm_op ops[16];
	int op_index = -1, last_op = sizeof(ops) / sizeof(ops[0]) - 1;

	if(mm == NULL) return;
	fd = mm->context.skas.mm_fd;
	for(addr = start_addr; addr < end_addr;){
		npgd = pgd_offset(mm, addr);
		if(!pgd_present(*npgd)){
			if(force || pgd_newpage(*npgd)){
				end = addr + PGDIR_SIZE;
				if(end > end_addr)
					end = end_addr;
				op_index = add_munmap(addr, end - addr, ops,
						      op_index, last_op, fd,
						      do_ops);
				pgd_mkuptodate(*npgd);
			}
			addr += PGDIR_SIZE;
			continue;
		}

		npud = pud_offset(npgd, addr);
		if(!pud_present(*npud)){
			if(force || pud_newpage(*npud)){
				end = addr + PUD_SIZE;
				if(end > end_addr)
					end = end_addr;
				op_index = add_munmap(addr, end - addr, ops,
						      op_index, last_op, fd,
						      do_ops);
				pud_mkuptodate(*npud);
			}
			addr += PUD_SIZE;
			continue;
		}

		npmd = pmd_offset(npud, addr);
		if(!pmd_present(*npmd)){
			if(force || pmd_newpage(*npmd)){
				end = addr + PMD_SIZE;
				if(end > end_addr)
					end = end_addr;
				op_index = add_munmap(addr, end - addr, ops,
						      op_index, last_op, fd,
						      do_ops);
				pmd_mkuptodate(*npmd);
			}
			addr += PMD_SIZE;
			continue;
		}

		npte = pte_offset_kernel(npmd, addr);
		r = pte_read(*npte);
		w = pte_write(*npte);
		x = pte_exec(*npte);
		if(!pte_dirty(*npte))
			w = 0;
		if(!pte_young(*npte)){
			r = 0;
			w = 0;
		}
		if(force || pte_newpage(*npte)){
			if(pte_present(*npte))
				op_index = add_mmap(addr,
						    pte_val(*npte) & PAGE_MASK,
						    PAGE_SIZE, r, w, x, ops,
						    op_index, last_op, fd,
						    do_ops);
			else op_index = add_munmap(addr, PAGE_SIZE, ops,
						   op_index, last_op, fd,
						   do_ops);
		}
		else if(pte_newprot(*npte))
			op_index = add_mprotect(addr, PAGE_SIZE, r, w, x, ops,
						op_index, last_op, fd,
						do_ops);

		*npte = pte_mkuptodate(*npte);
		addr += PAGE_SIZE;
	}
	do_ops(fd, ops, op_index);
}

void flush_tlb_kernel_range_skas(unsigned long start, unsigned long end)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, last;
	int updated = 0, err;

	mm = &init_mm;
	for(addr = start; addr < end;){
		pgd = pgd_offset(mm, addr);
		pud = pud_offset(pgd, addr);
		pmd = pmd_offset(pud, addr);
 		if(!pgd_present(*pgd)){
			if(pgd_newpage(*pgd)){
				updated = 1;
				last = addr + PGDIR_SIZE;
				if(last > end)
					last = end;
				err = os_unmap_memory((void *) addr, 
						      last - addr);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr += PGDIR_SIZE;
			continue;
		}

		pud = pud_offset(pgd, addr);
		if(!pud_present(*pud)){
			if(pud_newpage(*pud)){
				updated = 1;
				last = addr + PUD_SIZE;
				if(last > end)
					last = end;
				err = os_unmap_memory((void *) addr,
						      last - addr);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr += PUD_SIZE;
			continue;
		}

		pmd = pmd_offset(pud, addr);
		if(!pmd_present(*pmd)){
			if(pmd_newpage(*pmd)){
				updated = 1;
				last = addr + PMD_SIZE;
				if(last > end)
					last = end;
				err = os_unmap_memory((void *) addr,
						      last - addr);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr += PMD_SIZE;
			continue;
		}

		pte = pte_offset_kernel(pmd, addr);
		if(!pte_present(*pte) || pte_newpage(*pte)){
			updated = 1;
			err = os_unmap_memory((void *) addr, PAGE_SIZE);
			if(err < 0)
				panic("munmap failed, errno = %d\n", -err);
			if(pte_present(*pte))
				map_memory(addr, pte_val(*pte) & PAGE_MASK,
					   PAGE_SIZE, 1, 1, 1);
		}
		else if(pte_newprot(*pte)){
			updated = 1;
			protect_memory(addr, PAGE_SIZE, 1, 1, 1, 1);
  		}
		addr += PAGE_SIZE;
	}
}

void flush_tlb_kernel_vm_skas(void)
{
	flush_tlb_kernel_range_skas(start_vm, end_vm);
}

void __flush_tlb_one_skas(unsigned long addr)
{
	flush_tlb_kernel_range_skas(addr, addr + PAGE_SIZE);
}

void flush_tlb_range_skas(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
	if(vma->vm_mm == NULL)
		flush_tlb_kernel_range_skas(start, end);
	else fix_range(vma->vm_mm, start, end, 0);
}

void flush_tlb_mm_skas(struct mm_struct *mm)
{
	/* Don't bother flushing if this address space is about to be
	 * destroyed.
	 */
	if(atomic_read(&mm->mm_users) == 0)
		return;

	flush_tlb_kernel_vm_skas();
	fix_range(mm, 0, host_task_size, 0);
}

void force_flush_all_skas(void)
{
	fix_range(current->mm, 0, host_task_size, 1);
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
