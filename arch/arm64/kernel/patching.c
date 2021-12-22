// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/static_call.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/insn.h>
#include <asm/kprobes.h>
#include <asm/patching.h>
#include <asm/sections.h>

static DEFINE_RAW_SPINLOCK(patch_lock);

static bool is_exit_text(unsigned long addr)
{
	/* discarded with init text/data */
	return system_state < SYSTEM_RUNNING &&
		addr >= (unsigned long)__exittext_begin &&
		addr < (unsigned long)__exittext_end;
}

static bool is_image_text(unsigned long addr)
{
	return core_kernel_text(addr) || is_exit_text(addr);
}

static void __kprobes *patch_map(void *addr, int fixmap)
{
	unsigned long uintaddr = (uintptr_t) addr;
	bool image = is_image_text(uintaddr);
	struct page *page;

	if (image)
		page = phys_to_page(__pa_symbol(addr));
	else if (IS_ENABLED(CONFIG_STRICT_MODULE_RWX))
		page = vmalloc_to_page(addr);
	else
		return addr;

	BUG_ON(!page);
	return (void *)set_fixmap_offset(fixmap, page_to_phys(page) +
			(uintaddr & ~PAGE_MASK));
}

static void __kprobes patch_unmap(int fixmap)
{
	clear_fixmap(fixmap);
}
/*
 * In ARMv8-A, A64 instructions have a fixed length of 32 bits and are always
 * little-endian.
 */
int __kprobes aarch64_insn_read(void *addr, u32 *insnp)
{
	int ret;
	__le32 val;

	ret = copy_from_kernel_nofault(&val, addr, AARCH64_INSN_SIZE);
	if (!ret)
		*insnp = le32_to_cpu(val);

	return ret;
}

static int __kprobes __aarch64_insn_write(void *addr, void *insn, int size)
{
	void *waddr = addr;
	unsigned long flags = 0;
	int ret;

	raw_spin_lock_irqsave(&patch_lock, flags);
	waddr = patch_map(addr, FIX_TEXT_POKE0);

	ret = copy_to_kernel_nofault(waddr, insn, size);

	patch_unmap(FIX_TEXT_POKE0);
	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}

int __kprobes aarch64_insn_write(void *addr, u32 insn)
{
	__le32 i = cpu_to_le32(insn);

	return __aarch64_insn_write(addr, &i, AARCH64_INSN_SIZE);
}

static void *strip_cfi_jt(void *addr)
{
	if (IS_ENABLED(CONFIG_CFI_CLANG)) {
		void *p = addr;
		u32 insn;

		/*
		 * Taking the address of a function produces the address of the
		 * jump table entry when Clang CFI is enabled. Such entries are
		 * ordinary jump instructions, preceded by a BTI C instruction
		 * if BTI is enabled for the kernel.
		 */
		if (IS_ENABLED(CONFIG_ARM64_BTI_KERNEL))
			p += 4;

		insn = le32_to_cpup(p);
		if (aarch64_insn_is_b(insn))
			return p + aarch64_get_branch_offset(insn);

		WARN_ON(1);
	}
	return addr;
}

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	/*
	 * -0x8	<literal>
	 *  0x0	bti c		<--- trampoline entry point
	 *  0x4	<branch or nop>
	 *  0x8	ldr x16, <literal>
	 *  0xc	cbz x16, 20
	 * 0x10	br x16
	 * 0x14	ret
	 */
	struct {
		u64	literal;
		__le32	insn[2];
	} insns;
	u32 insn;
	int ret;

	insn = aarch64_insn_gen_hint(AARCH64_INSN_HINT_BTIC);
	insns.literal = (u64)func;
	insns.insn[0] = cpu_to_le32(insn);

	if (!func) {
		insn = aarch64_insn_gen_branch_reg(AARCH64_INSN_REG_LR,
						   AARCH64_INSN_BRANCH_RETURN);
	} else {
		insn = aarch64_insn_gen_branch_imm((u64)tramp + 4,
						   (u64)strip_cfi_jt(func),
						   AARCH64_INSN_BRANCH_NOLINK);

		/*
		 * Use a NOP if the branch target is out of range, and rely on
		 * the indirect call instead.
		 */
		if (insn == AARCH64_BREAK_FAULT)
			insn = aarch64_insn_gen_hint(AARCH64_INSN_HINT_NOP);
	}
	insns.insn[1] = cpu_to_le32(insn);

	ret = __aarch64_insn_write(tramp - 8, &insns, sizeof(insns));
	if (!WARN_ON(ret))
		caches_clean_inval_pou((u64)tramp - 8, sizeof(insns));
}

int __kprobes aarch64_insn_patch_text_nosync(void *addr, u32 insn)
{
	u32 *tp = addr;
	int ret;

	/* A64 instructions must be word aligned */
	if ((uintptr_t)tp & 0x3)
		return -EINVAL;

	ret = aarch64_insn_write(tp, insn);
	if (ret == 0)
		caches_clean_inval_pou((uintptr_t)tp,
				     (uintptr_t)tp + AARCH64_INSN_SIZE);

	return ret;
}

struct aarch64_insn_patch {
	void		**text_addrs;
	u32		*new_insns;
	int		insn_cnt;
	atomic_t	cpu_count;
};

static int __kprobes aarch64_insn_patch_text_cb(void *arg)
{
	int i, ret = 0;
	struct aarch64_insn_patch *pp = arg;

	/* The first CPU becomes master */
	if (atomic_inc_return(&pp->cpu_count) == 1) {
		for (i = 0; ret == 0 && i < pp->insn_cnt; i++)
			ret = aarch64_insn_patch_text_nosync(pp->text_addrs[i],
							     pp->new_insns[i]);
		/* Notify other processors with an additional increment. */
		atomic_inc(&pp->cpu_count);
	} else {
		while (atomic_read(&pp->cpu_count) <= num_online_cpus())
			cpu_relax();
		isb();
	}

	return ret;
}

int __kprobes aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt)
{
	struct aarch64_insn_patch patch = {
		.text_addrs = addrs,
		.new_insns = insns,
		.insn_cnt = cnt,
		.cpu_count = ATOMIC_INIT(0),
	};

	if (cnt <= 0)
		return -EINVAL;

	return stop_machine_cpuslocked(aarch64_insn_patch_text_cb, &patch,
				       cpu_online_mask);
}
