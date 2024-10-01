// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/mmu_context.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/cpuhotplug.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/debug.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/code-patching.h>
#include <asm/setup.h>
#include <asm/inst.h>

static int __patch_mem(void *exec_addr, unsigned long val, void *patch_addr, bool is_dword)
{
	if (!IS_ENABLED(CONFIG_PPC64) || likely(!is_dword)) {
		/* For big endian correctness: plain address would use the wrong half */
		u32 val32 = val;

		__put_kernel_nofault(patch_addr, &val32, u32, failed);
	} else {
		__put_kernel_nofault(patch_addr, &val, u64, failed);
	}

	asm ("dcbst 0, %0; sync; icbi 0,%1; sync; isync" :: "r" (patch_addr),
							    "r" (exec_addr));

	return 0;

failed:
	mb();  /* sync */
	return -EPERM;
}

int raw_patch_instruction(u32 *addr, struct ppc_inst instr)
{
	if (ppc_inst_prefixed(instr))
		return __patch_mem(addr, ppc_inst_as_ulong(instr), addr, true);
	else
		return __patch_mem(addr, ppc_inst_val(instr), addr, false);
}

struct patch_context {
	union {
		struct vm_struct *area;
		struct mm_struct *mm;
	};
	unsigned long addr;
	pte_t *pte;
};

static DEFINE_PER_CPU(struct patch_context, cpu_patching_context);

static int map_patch_area(void *addr, unsigned long text_poke_addr);
static void unmap_patch_area(unsigned long addr);

static bool mm_patch_enabled(void)
{
	return IS_ENABLED(CONFIG_SMP) && radix_enabled();
}

/*
 * The following applies for Radix MMU. Hash MMU has different requirements,
 * and so is not supported.
 *
 * Changing mm requires context synchronising instructions on both sides of
 * the context switch, as well as a hwsync between the last instruction for
 * which the address of an associated storage access was translated using
 * the current context.
 *
 * switch_mm_irqs_off() performs an isync after the context switch. It is
 * the responsibility of the caller to perform the CSI and hwsync before
 * starting/stopping the temp mm.
 */
static struct mm_struct *start_using_temp_mm(struct mm_struct *temp_mm)
{
	struct mm_struct *orig_mm = current->active_mm;

	lockdep_assert_irqs_disabled();
	switch_mm_irqs_off(orig_mm, temp_mm, current);

	WARN_ON(!mm_is_thread_local(temp_mm));

	suspend_breakpoints();
	return orig_mm;
}

static void stop_using_temp_mm(struct mm_struct *temp_mm,
			       struct mm_struct *orig_mm)
{
	lockdep_assert_irqs_disabled();
	switch_mm_irqs_off(temp_mm, orig_mm, current);
	restore_breakpoints();
}

static int text_area_cpu_up(unsigned int cpu)
{
	struct vm_struct *area;
	unsigned long addr;
	int err;

	area = get_vm_area(PAGE_SIZE, VM_ALLOC);
	if (!area) {
		WARN_ONCE(1, "Failed to create text area for cpu %d\n",
			cpu);
		return -1;
	}

	// Map/unmap the area to ensure all page tables are pre-allocated
	addr = (unsigned long)area->addr;
	err = map_patch_area(empty_zero_page, addr);
	if (err)
		return err;

	unmap_patch_area(addr);

	this_cpu_write(cpu_patching_context.area, area);
	this_cpu_write(cpu_patching_context.addr, addr);
	this_cpu_write(cpu_patching_context.pte, virt_to_kpte(addr));

	return 0;
}

static int text_area_cpu_down(unsigned int cpu)
{
	free_vm_area(this_cpu_read(cpu_patching_context.area));
	this_cpu_write(cpu_patching_context.area, NULL);
	this_cpu_write(cpu_patching_context.addr, 0);
	this_cpu_write(cpu_patching_context.pte, NULL);
	return 0;
}

static void put_patching_mm(struct mm_struct *mm, unsigned long patching_addr)
{
	struct mmu_gather tlb;

	tlb_gather_mmu(&tlb, mm);
	free_pgd_range(&tlb, patching_addr, patching_addr + PAGE_SIZE, 0, 0);
	mmput(mm);
}

static int text_area_cpu_up_mm(unsigned int cpu)
{
	struct mm_struct *mm;
	unsigned long addr;
	pte_t *pte;
	spinlock_t *ptl;

	mm = mm_alloc();
	if (WARN_ON(!mm))
		goto fail_no_mm;

	/*
	 * Choose a random page-aligned address from the interval
	 * [PAGE_SIZE .. DEFAULT_MAP_WINDOW - PAGE_SIZE].
	 * The lower address bound is PAGE_SIZE to avoid the zero-page.
	 */
	addr = (1 + (get_random_long() % (DEFAULT_MAP_WINDOW / PAGE_SIZE - 2))) << PAGE_SHIFT;

	/*
	 * PTE allocation uses GFP_KERNEL which means we need to
	 * pre-allocate the PTE here because we cannot do the
	 * allocation during patching when IRQs are disabled.
	 *
	 * Using get_locked_pte() to avoid open coding, the lock
	 * is unnecessary.
	 */
	pte = get_locked_pte(mm, addr, &ptl);
	if (!pte)
		goto fail_no_pte;
	pte_unmap_unlock(pte, ptl);

	this_cpu_write(cpu_patching_context.mm, mm);
	this_cpu_write(cpu_patching_context.addr, addr);

	return 0;

fail_no_pte:
	put_patching_mm(mm, addr);
fail_no_mm:
	return -ENOMEM;
}

static int text_area_cpu_down_mm(unsigned int cpu)
{
	put_patching_mm(this_cpu_read(cpu_patching_context.mm),
			this_cpu_read(cpu_patching_context.addr));

	this_cpu_write(cpu_patching_context.mm, NULL);
	this_cpu_write(cpu_patching_context.addr, 0);

	return 0;
}

static __ro_after_init DEFINE_STATIC_KEY_FALSE(poking_init_done);

void __init poking_init(void)
{
	int ret;

	if (mm_patch_enabled())
		ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
					"powerpc/text_poke_mm:online",
					text_area_cpu_up_mm,
					text_area_cpu_down_mm);
	else
		ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
					"powerpc/text_poke:online",
					text_area_cpu_up,
					text_area_cpu_down);

	/* cpuhp_setup_state returns >= 0 on success */
	if (WARN_ON(ret < 0))
		return;

	static_branch_enable(&poking_init_done);
}

static unsigned long get_patch_pfn(void *addr)
{
	if (IS_ENABLED(CONFIG_MODULES) && is_vmalloc_or_module_addr(addr))
		return vmalloc_to_pfn(addr);
	else
		return __pa_symbol(addr) >> PAGE_SHIFT;
}

/*
 * This can be called for kernel text or a module.
 */
static int map_patch_area(void *addr, unsigned long text_poke_addr)
{
	unsigned long pfn = get_patch_pfn(addr);

	return map_kernel_page(text_poke_addr, (pfn << PAGE_SHIFT), PAGE_KERNEL);
}

static void unmap_patch_area(unsigned long addr)
{
	pte_t *ptep;
	pmd_t *pmdp;
	pud_t *pudp;
	p4d_t *p4dp;
	pgd_t *pgdp;

	pgdp = pgd_offset_k(addr);
	if (WARN_ON(pgd_none(*pgdp)))
		return;

	p4dp = p4d_offset(pgdp, addr);
	if (WARN_ON(p4d_none(*p4dp)))
		return;

	pudp = pud_offset(p4dp, addr);
	if (WARN_ON(pud_none(*pudp)))
		return;

	pmdp = pmd_offset(pudp, addr);
	if (WARN_ON(pmd_none(*pmdp)))
		return;

	ptep = pte_offset_kernel(pmdp, addr);
	if (WARN_ON(pte_none(*ptep)))
		return;

	/*
	 * In hash, pte_clear flushes the tlb, in radix, we have to
	 */
	pte_clear(&init_mm, addr, ptep);
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
}

static int __do_patch_mem_mm(void *addr, unsigned long val, bool is_dword)
{
	int err;
	u32 *patch_addr;
	unsigned long text_poke_addr;
	pte_t *pte;
	unsigned long pfn = get_patch_pfn(addr);
	struct mm_struct *patching_mm;
	struct mm_struct *orig_mm;
	spinlock_t *ptl;

	patching_mm = __this_cpu_read(cpu_patching_context.mm);
	text_poke_addr = __this_cpu_read(cpu_patching_context.addr);
	patch_addr = (u32 *)(text_poke_addr + offset_in_page(addr));

	pte = get_locked_pte(patching_mm, text_poke_addr, &ptl);
	if (!pte)
		return -ENOMEM;

	__set_pte_at(patching_mm, text_poke_addr, pte, pfn_pte(pfn, PAGE_KERNEL), 0);

	/* order PTE update before use, also serves as the hwsync */
	asm volatile("ptesync": : :"memory");

	/* order context switch after arbitrary prior code */
	isync();

	orig_mm = start_using_temp_mm(patching_mm);

	err = __patch_mem(addr, val, patch_addr, is_dword);

	/* context synchronisation performed by __patch_instruction (isync or exception) */
	stop_using_temp_mm(patching_mm, orig_mm);

	pte_clear(patching_mm, text_poke_addr, pte);
	/*
	 * ptesync to order PTE update before TLB invalidation done
	 * by radix__local_flush_tlb_page_psize (in _tlbiel_va)
	 */
	local_flush_tlb_page_psize(patching_mm, text_poke_addr, mmu_virtual_psize);

	pte_unmap_unlock(pte, ptl);

	return err;
}

static int __do_patch_mem(void *addr, unsigned long val, bool is_dword)
{
	int err;
	u32 *patch_addr;
	unsigned long text_poke_addr;
	pte_t *pte;
	unsigned long pfn = get_patch_pfn(addr);

	text_poke_addr = (unsigned long)__this_cpu_read(cpu_patching_context.addr) & PAGE_MASK;
	patch_addr = (u32 *)(text_poke_addr + offset_in_page(addr));

	pte = __this_cpu_read(cpu_patching_context.pte);
	__set_pte_at(&init_mm, text_poke_addr, pte, pfn_pte(pfn, PAGE_KERNEL), 0);
	/* See ptesync comment in radix__set_pte_at() */
	if (radix_enabled())
		asm volatile("ptesync": : :"memory");

	err = __patch_mem(addr, val, patch_addr, is_dword);

	pte_clear(&init_mm, text_poke_addr, pte);
	flush_tlb_kernel_range(text_poke_addr, text_poke_addr + PAGE_SIZE);

	return err;
}

static int patch_mem(void *addr, unsigned long val, bool is_dword)
{
	int err;
	unsigned long flags;

	/*
	 * During early early boot patch_instruction is called
	 * when text_poke_area is not ready, but we still need
	 * to allow patching. We just do the plain old patching
	 */
	if (!IS_ENABLED(CONFIG_STRICT_KERNEL_RWX) ||
	    !static_branch_likely(&poking_init_done))
		return __patch_mem(addr, val, addr, is_dword);

	local_irq_save(flags);
	if (mm_patch_enabled())
		err = __do_patch_mem_mm(addr, val, is_dword);
	else
		err = __do_patch_mem(addr, val, is_dword);
	local_irq_restore(flags);

	return err;
}

#ifdef CONFIG_PPC64

static int do_patch_instruction(u32 *addr, struct ppc_inst instr)
{
	if (ppc_inst_prefixed(instr))
		return patch_mem(addr, ppc_inst_as_ulong(instr), true);
	else
		return patch_mem(addr, ppc_inst_val(instr), false);
}

int patch_uint(void *addr, unsigned int val)
{
	return patch_mem(addr, val, false);
}
NOKPROBE_SYMBOL(patch_uint);

int patch_ulong(void *addr, unsigned long val)
{
	return patch_mem(addr, val, true);
}
NOKPROBE_SYMBOL(patch_ulong);

#else

int patch_instruction(u32 *addr, struct ppc_inst instr)
{
	return patch_mem(addr, ppc_inst_val(instr), false);
}
NOKPROBE_SYMBOL(patch_instruction)

#endif

int patch_instruction(u32 *addr, struct ppc_inst instr)
{
	/* Make sure we aren't patching a freed init section */
	if (init_mem_is_free && init_section_contains(addr, 4))
		return 0;

	return do_patch_instruction(addr, instr);
}
NOKPROBE_SYMBOL(patch_instruction);

static int __patch_instructions(u32 *patch_addr, u32 *code, size_t len, bool repeat_instr)
{
	unsigned long start = (unsigned long)patch_addr;

	/* Repeat instruction */
	if (repeat_instr) {
		struct ppc_inst instr = ppc_inst_read(code);

		if (ppc_inst_prefixed(instr)) {
			u64 val = ppc_inst_as_ulong(instr);

			memset64((u64 *)patch_addr, val, len / 8);
		} else {
			u32 val = ppc_inst_val(instr);

			memset32(patch_addr, val, len / 4);
		}
	} else {
		memcpy(patch_addr, code, len);
	}

	smp_wmb();	/* smp write barrier */
	flush_icache_range(start, start + len);
	return 0;
}

/*
 * A page is mapped and instructions that fit the page are patched.
 * Assumes 'len' to be (PAGE_SIZE - offset_in_page(addr)) or below.
 */
static int __do_patch_instructions_mm(u32 *addr, u32 *code, size_t len, bool repeat_instr)
{
	struct mm_struct *patching_mm, *orig_mm;
	unsigned long pfn = get_patch_pfn(addr);
	unsigned long text_poke_addr;
	spinlock_t *ptl;
	u32 *patch_addr;
	pte_t *pte;
	int err;

	patching_mm = __this_cpu_read(cpu_patching_context.mm);
	text_poke_addr = __this_cpu_read(cpu_patching_context.addr);
	patch_addr = (u32 *)(text_poke_addr + offset_in_page(addr));

	pte = get_locked_pte(patching_mm, text_poke_addr, &ptl);
	if (!pte)
		return -ENOMEM;

	__set_pte_at(patching_mm, text_poke_addr, pte, pfn_pte(pfn, PAGE_KERNEL), 0);

	/* order PTE update before use, also serves as the hwsync */
	asm volatile("ptesync" ::: "memory");

	/* order context switch after arbitrary prior code */
	isync();

	orig_mm = start_using_temp_mm(patching_mm);

	err = __patch_instructions(patch_addr, code, len, repeat_instr);

	/* context synchronisation performed by __patch_instructions */
	stop_using_temp_mm(patching_mm, orig_mm);

	pte_clear(patching_mm, text_poke_addr, pte);
	/*
	 * ptesync to order PTE update before TLB invalidation done
	 * by radix__local_flush_tlb_page_psize (in _tlbiel_va)
	 */
	local_flush_tlb_page_psize(patching_mm, text_poke_addr, mmu_virtual_psize);

	pte_unmap_unlock(pte, ptl);

	return err;
}

/*
 * A page is mapped and instructions that fit the page are patched.
 * Assumes 'len' to be (PAGE_SIZE - offset_in_page(addr)) or below.
 */
static int __do_patch_instructions(u32 *addr, u32 *code, size_t len, bool repeat_instr)
{
	unsigned long pfn = get_patch_pfn(addr);
	unsigned long text_poke_addr;
	u32 *patch_addr;
	pte_t *pte;
	int err;

	text_poke_addr = (unsigned long)__this_cpu_read(cpu_patching_context.addr) & PAGE_MASK;
	patch_addr = (u32 *)(text_poke_addr + offset_in_page(addr));

	pte = __this_cpu_read(cpu_patching_context.pte);
	__set_pte_at(&init_mm, text_poke_addr, pte, pfn_pte(pfn, PAGE_KERNEL), 0);
	/* See ptesync comment in radix__set_pte_at() */
	if (radix_enabled())
		asm volatile("ptesync" ::: "memory");

	err = __patch_instructions(patch_addr, code, len, repeat_instr);

	pte_clear(&init_mm, text_poke_addr, pte);
	flush_tlb_kernel_range(text_poke_addr, text_poke_addr + PAGE_SIZE);

	return err;
}

/*
 * Patch 'addr' with 'len' bytes of instructions from 'code'.
 *
 * If repeat_instr is true, the same instruction is filled for
 * 'len' bytes.
 */
int patch_instructions(u32 *addr, u32 *code, size_t len, bool repeat_instr)
{
	while (len > 0) {
		unsigned long flags;
		size_t plen;
		int err;

		plen = min_t(size_t, PAGE_SIZE - offset_in_page(addr), len);

		local_irq_save(flags);
		if (mm_patch_enabled())
			err = __do_patch_instructions_mm(addr, code, plen, repeat_instr);
		else
			err = __do_patch_instructions(addr, code, plen, repeat_instr);
		local_irq_restore(flags);
		if (err)
			return err;

		len -= plen;
		addr = (u32 *)((unsigned long)addr + plen);
		if (!repeat_instr)
			code = (u32 *)((unsigned long)code + plen);
	}

	return 0;
}
NOKPROBE_SYMBOL(patch_instructions);

int patch_branch(u32 *addr, unsigned long target, int flags)
{
	struct ppc_inst instr;

	create_branch(&instr, addr, target, flags);
	return patch_instruction(addr, instr);
}

bool is_offset_in_branch_range(long offset)
{
	/*
	 * Powerpc branch instruction is :
	 *
	 *  0         6                 30   31
	 *  +---------+----------------+---+---+
	 *  | opcode  |     LI         |AA |LK |
	 *  +---------+----------------+---+---+
	 *  Where AA = 0 and LK = 0
	 *
	 * LI is a signed 24 bits integer. The real branch offset is computed
	 * by: imm32 = SignExtend(LI:'0b00', 32);
	 *
	 * So the maximum forward branch should be:
	 *   (0x007fffff << 2) = 0x01fffffc =  0x1fffffc
	 * The maximum backward branch should be:
	 *   (0xff800000 << 2) = 0xfe000000 = -0x2000000
	 */
	return (offset >= -0x2000000 && offset <= 0x1fffffc && !(offset & 0x3));
}

bool is_offset_in_cond_branch_range(long offset)
{
	return offset >= -0x8000 && offset <= 0x7fff && !(offset & 0x3);
}

/*
 * Helper to check if a given instruction is a conditional branch
 * Derived from the conditional checks in analyse_instr()
 */
bool is_conditional_branch(struct ppc_inst instr)
{
	unsigned int opcode = ppc_inst_primary_opcode(instr);

	if (opcode == 16)       /* bc, bca, bcl, bcla */
		return true;
	if (opcode == 19) {
		switch ((ppc_inst_val(instr) >> 1) & 0x3ff) {
		case 16:        /* bclr, bclrl */
		case 528:       /* bcctr, bcctrl */
		case 560:       /* bctar, bctarl */
			return true;
		}
	}
	return false;
}
NOKPROBE_SYMBOL(is_conditional_branch);

int create_branch(struct ppc_inst *instr, const u32 *addr,
		  unsigned long target, int flags)
{
	long offset;

	*instr = ppc_inst(0);
	offset = target;
	if (! (flags & BRANCH_ABSOLUTE))
		offset = offset - (unsigned long)addr;

	/* Check we can represent the target in the instruction format */
	if (!is_offset_in_branch_range(offset))
		return 1;

	/* Mask out the flags and target, so they don't step on each other. */
	*instr = ppc_inst(0x48000000 | (flags & 0x3) | (offset & 0x03FFFFFC));

	return 0;
}

int create_cond_branch(struct ppc_inst *instr, const u32 *addr,
		       unsigned long target, int flags)
{
	long offset;

	offset = target;
	if (! (flags & BRANCH_ABSOLUTE))
		offset = offset - (unsigned long)addr;

	/* Check we can represent the target in the instruction format */
	if (!is_offset_in_cond_branch_range(offset))
		return 1;

	/* Mask out the flags and target, so they don't step on each other. */
	*instr = ppc_inst(0x40000000 | (flags & 0x3FF0003) | (offset & 0xFFFC));

	return 0;
}

static unsigned int branch_opcode(struct ppc_inst instr)
{
	return ppc_inst_primary_opcode(instr) & 0x3F;
}

static int instr_is_branch_iform(struct ppc_inst instr)
{
	return branch_opcode(instr) == 18;
}

static int instr_is_branch_bform(struct ppc_inst instr)
{
	return branch_opcode(instr) == 16;
}

int instr_is_relative_branch(struct ppc_inst instr)
{
	if (ppc_inst_val(instr) & BRANCH_ABSOLUTE)
		return 0;

	return instr_is_branch_iform(instr) || instr_is_branch_bform(instr);
}

int instr_is_relative_link_branch(struct ppc_inst instr)
{
	return instr_is_relative_branch(instr) && (ppc_inst_val(instr) & BRANCH_SET_LINK);
}

static unsigned long branch_iform_target(const u32 *instr)
{
	signed long imm;

	imm = ppc_inst_val(ppc_inst_read(instr)) & 0x3FFFFFC;

	/* If the top bit of the immediate value is set this is negative */
	if (imm & 0x2000000)
		imm -= 0x4000000;

	if ((ppc_inst_val(ppc_inst_read(instr)) & BRANCH_ABSOLUTE) == 0)
		imm += (unsigned long)instr;

	return (unsigned long)imm;
}

static unsigned long branch_bform_target(const u32 *instr)
{
	signed long imm;

	imm = ppc_inst_val(ppc_inst_read(instr)) & 0xFFFC;

	/* If the top bit of the immediate value is set this is negative */
	if (imm & 0x8000)
		imm -= 0x10000;

	if ((ppc_inst_val(ppc_inst_read(instr)) & BRANCH_ABSOLUTE) == 0)
		imm += (unsigned long)instr;

	return (unsigned long)imm;
}

unsigned long branch_target(const u32 *instr)
{
	if (instr_is_branch_iform(ppc_inst_read(instr)))
		return branch_iform_target(instr);
	else if (instr_is_branch_bform(ppc_inst_read(instr)))
		return branch_bform_target(instr);

	return 0;
}

int translate_branch(struct ppc_inst *instr, const u32 *dest, const u32 *src)
{
	unsigned long target;
	target = branch_target(src);

	if (instr_is_branch_iform(ppc_inst_read(src)))
		return create_branch(instr, dest, target,
				     ppc_inst_val(ppc_inst_read(src)));
	else if (instr_is_branch_bform(ppc_inst_read(src)))
		return create_cond_branch(instr, dest, target,
					  ppc_inst_val(ppc_inst_read(src)));

	return 1;
}

#ifdef CONFIG_PPC_BOOK3E_64
void __patch_exception(int exc, unsigned long addr)
{
	extern unsigned int interrupt_base_book3e;
	unsigned int *ibase = &interrupt_base_book3e;

	/* Our exceptions vectors start with a NOP and -then- a branch
	 * to deal with single stepping from userspace which stops on
	 * the second instruction. Thus we need to patch the second
	 * instruction of the exception, not the first one
	 */

	patch_branch(ibase + (exc / 4) + 1, addr, 0);
}
#endif

#ifdef CONFIG_CODE_PATCHING_SELFTEST

static int __init instr_is_branch_to_addr(const u32 *instr, unsigned long addr)
{
	if (instr_is_branch_iform(ppc_inst_read(instr)) ||
	    instr_is_branch_bform(ppc_inst_read(instr)))
		return branch_target(instr) == addr;

	return 0;
}

static void __init test_trampoline(void)
{
	asm ("nop;\n");
}

#define check(x)	do {	\
	if (!(x))		\
		pr_err("code-patching: test failed at line %d\n", __LINE__); \
} while (0)

static void __init test_branch_iform(void)
{
	int err;
	struct ppc_inst instr;
	u32 tmp[2];
	u32 *iptr = tmp;
	unsigned long addr = (unsigned long)tmp;

	/* The simplest case, branch to self, no flags */
	check(instr_is_branch_iform(ppc_inst(0x48000000)));
	/* All bits of target set, and flags */
	check(instr_is_branch_iform(ppc_inst(0x4bffffff)));
	/* High bit of opcode set, which is wrong */
	check(!instr_is_branch_iform(ppc_inst(0xcbffffff)));
	/* Middle bits of opcode set, which is wrong */
	check(!instr_is_branch_iform(ppc_inst(0x7bffffff)));

	/* Simplest case, branch to self with link */
	check(instr_is_branch_iform(ppc_inst(0x48000001)));
	/* All bits of targets set */
	check(instr_is_branch_iform(ppc_inst(0x4bfffffd)));
	/* Some bits of targets set */
	check(instr_is_branch_iform(ppc_inst(0x4bff00fd)));
	/* Must be a valid branch to start with */
	check(!instr_is_branch_iform(ppc_inst(0x7bfffffd)));

	/* Absolute branch to 0x100 */
	patch_instruction(iptr, ppc_inst(0x48000103));
	check(instr_is_branch_to_addr(iptr, 0x100));
	/* Absolute branch to 0x420fc */
	patch_instruction(iptr, ppc_inst(0x480420ff));
	check(instr_is_branch_to_addr(iptr, 0x420fc));
	/* Maximum positive relative branch, + 20MB - 4B */
	patch_instruction(iptr, ppc_inst(0x49fffffc));
	check(instr_is_branch_to_addr(iptr, addr + 0x1FFFFFC));
	/* Smallest negative relative branch, - 4B */
	patch_instruction(iptr, ppc_inst(0x4bfffffc));
	check(instr_is_branch_to_addr(iptr, addr - 4));
	/* Largest negative relative branch, - 32 MB */
	patch_instruction(iptr, ppc_inst(0x4a000000));
	check(instr_is_branch_to_addr(iptr, addr - 0x2000000));

	/* Branch to self, with link */
	err = create_branch(&instr, iptr, addr, BRANCH_SET_LINK);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));

	/* Branch to self - 0x100, with link */
	err = create_branch(&instr, iptr, addr - 0x100, BRANCH_SET_LINK);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x100));

	/* Branch to self + 0x100, no link */
	err = create_branch(&instr, iptr, addr + 0x100, 0);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr + 0x100));

	/* Maximum relative negative offset, - 32 MB */
	err = create_branch(&instr, iptr, addr - 0x2000000, BRANCH_SET_LINK);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x2000000));

	/* Out of range relative negative offset, - 32 MB + 4*/
	err = create_branch(&instr, iptr, addr - 0x2000004, BRANCH_SET_LINK);
	check(err);

	/* Out of range relative positive offset, + 32 MB */
	err = create_branch(&instr, iptr, addr + 0x2000000, BRANCH_SET_LINK);
	check(err);

	/* Unaligned target */
	err = create_branch(&instr, iptr, addr + 3, BRANCH_SET_LINK);
	check(err);

	/* Check flags are masked correctly */
	err = create_branch(&instr, iptr, addr, 0xFFFFFFFC);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));
	check(ppc_inst_equal(instr, ppc_inst(0x48000000)));
}

static void __init test_create_function_call(void)
{
	u32 *iptr;
	unsigned long dest;
	struct ppc_inst instr;

	/* Check we can create a function call */
	iptr = (u32 *)ppc_function_entry(test_trampoline);
	dest = ppc_function_entry(test_create_function_call);
	create_branch(&instr, iptr, dest, BRANCH_SET_LINK);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, dest));
}

static void __init test_branch_bform(void)
{
	int err;
	unsigned long addr;
	struct ppc_inst instr;
	u32 tmp[2];
	u32 *iptr = tmp;
	unsigned int flags;

	addr = (unsigned long)iptr;

	/* The simplest case, branch to self, no flags */
	check(instr_is_branch_bform(ppc_inst(0x40000000)));
	/* All bits of target set, and flags */
	check(instr_is_branch_bform(ppc_inst(0x43ffffff)));
	/* High bit of opcode set, which is wrong */
	check(!instr_is_branch_bform(ppc_inst(0xc3ffffff)));
	/* Middle bits of opcode set, which is wrong */
	check(!instr_is_branch_bform(ppc_inst(0x7bffffff)));

	/* Absolute conditional branch to 0x100 */
	patch_instruction(iptr, ppc_inst(0x43ff0103));
	check(instr_is_branch_to_addr(iptr, 0x100));
	/* Absolute conditional branch to 0x20fc */
	patch_instruction(iptr, ppc_inst(0x43ff20ff));
	check(instr_is_branch_to_addr(iptr, 0x20fc));
	/* Maximum positive relative conditional branch, + 32 KB - 4B */
	patch_instruction(iptr, ppc_inst(0x43ff7ffc));
	check(instr_is_branch_to_addr(iptr, addr + 0x7FFC));
	/* Smallest negative relative conditional branch, - 4B */
	patch_instruction(iptr, ppc_inst(0x43fffffc));
	check(instr_is_branch_to_addr(iptr, addr - 4));
	/* Largest negative relative conditional branch, - 32 KB */
	patch_instruction(iptr, ppc_inst(0x43ff8000));
	check(instr_is_branch_to_addr(iptr, addr - 0x8000));

	/* All condition code bits set & link */
	flags = 0x3ff000 | BRANCH_SET_LINK;

	/* Branch to self */
	err = create_cond_branch(&instr, iptr, addr, flags);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));

	/* Branch to self - 0x100 */
	err = create_cond_branch(&instr, iptr, addr - 0x100, flags);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x100));

	/* Branch to self + 0x100 */
	err = create_cond_branch(&instr, iptr, addr + 0x100, flags);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr + 0x100));

	/* Maximum relative negative offset, - 32 KB */
	err = create_cond_branch(&instr, iptr, addr - 0x8000, flags);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x8000));

	/* Out of range relative negative offset, - 32 KB + 4*/
	err = create_cond_branch(&instr, iptr, addr - 0x8004, flags);
	check(err);

	/* Out of range relative positive offset, + 32 KB */
	err = create_cond_branch(&instr, iptr, addr + 0x8000, flags);
	check(err);

	/* Unaligned target */
	err = create_cond_branch(&instr, iptr, addr + 3, flags);
	check(err);

	/* Check flags are masked correctly */
	err = create_cond_branch(&instr, iptr, addr, 0xFFFFFFFC);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));
	check(ppc_inst_equal(instr, ppc_inst(0x43FF0000)));
}

static void __init test_translate_branch(void)
{
	unsigned long addr;
	void *p, *q;
	struct ppc_inst instr;
	void *buf;

	buf = vmalloc(PAGE_ALIGN(0x2000000 + 1));
	check(buf);
	if (!buf)
		return;

	/* Simple case, branch to self moved a little */
	p = buf;
	addr = (unsigned long)p;
	patch_branch(p, addr, 0);
	check(instr_is_branch_to_addr(p, addr));
	q = p + 4;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(q, addr));

	/* Maximum negative case, move b . to addr + 32 MB */
	p = buf;
	addr = (unsigned long)p;
	patch_branch(p, addr, 0);
	q = buf + 0x2000000;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x4a000000)));

	/* Maximum positive case, move x to x - 32 MB + 4 */
	p = buf + 0x2000000;
	addr = (unsigned long)p;
	patch_branch(p, addr, 0);
	q = buf + 4;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x49fffffc)));

	/* Jump to x + 16 MB moved to x + 20 MB */
	p = buf;
	addr = 0x1000000 + (unsigned long)buf;
	patch_branch(p, addr, BRANCH_SET_LINK);
	q = buf + 0x1400000;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Jump to x + 16 MB moved to x - 16 MB + 4 */
	p = buf + 0x1000000;
	addr = 0x2000000 + (unsigned long)buf;
	patch_branch(p, addr, 0);
	q = buf + 4;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));


	/* Conditional branch tests */

	/* Simple case, branch to self moved a little */
	p = buf;
	addr = (unsigned long)p;
	create_cond_branch(&instr, p, addr, 0);
	patch_instruction(p, instr);
	check(instr_is_branch_to_addr(p, addr));
	q = buf + 4;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(q, addr));

	/* Maximum negative case, move b . to addr + 32 KB */
	p = buf;
	addr = (unsigned long)p;
	create_cond_branch(&instr, p, addr, 0xFFFFFFFC);
	patch_instruction(p, instr);
	q = buf + 0x8000;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x43ff8000)));

	/* Maximum positive case, move x to x - 32 KB + 4 */
	p = buf + 0x8000;
	addr = (unsigned long)p;
	create_cond_branch(&instr, p, addr, 0xFFFFFFFC);
	patch_instruction(p, instr);
	q = buf + 4;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x43ff7ffc)));

	/* Jump to x + 12 KB moved to x + 20 KB */
	p = buf;
	addr = 0x3000 + (unsigned long)buf;
	create_cond_branch(&instr, p, addr, BRANCH_SET_LINK);
	patch_instruction(p, instr);
	q = buf + 0x5000;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Jump to x + 8 KB moved to x - 8 KB + 4 */
	p = buf + 0x2000;
	addr = 0x4000 + (unsigned long)buf;
	create_cond_branch(&instr, p, addr, 0);
	patch_instruction(p, instr);
	q = buf + 4;
	translate_branch(&instr, q, p);
	patch_instruction(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Free the buffer we were using */
	vfree(buf);
}

#ifdef CONFIG_PPC64
static void __init test_prefixed_patching(void)
{
	extern unsigned int code_patching_test1[];
	extern unsigned int code_patching_test1_expected[];
	extern unsigned int end_code_patching_test1[];

	__patch_instruction(code_patching_test1,
			    ppc_inst_prefix(OP_PREFIX << 26, 0x00000000),
			    code_patching_test1);

	check(!memcmp(code_patching_test1,
		      code_patching_test1_expected,
		      sizeof(unsigned int) *
		      (end_code_patching_test1 - code_patching_test1)));
}
#else
static inline void test_prefixed_patching(void) {}
#endif

static int __init test_code_patching(void)
{
	pr_info("Running code patching self-tests ...\n");

	test_branch_iform();
	test_branch_bform();
	test_create_function_call();
	test_translate_branch();
	test_prefixed_patching();

	return 0;
}
late_initcall(test_code_patching);

#endif /* CONFIG_CODE_PATCHING_SELFTEST */
