/*
 *  Copyright (C) 1994  Linus Torvalds
 *
 *  Cyrix stuff, June 1998 by:
 *	- Rafael R. Reilova (moved everything from head.S),
 *        <rreilova@ececs.uc.edu>
 *	- Channing Corn (tests & fixes),
 *	- Andrew D. Balsa (code cleanup).
 */
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/prctl.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/cpu.h>
#include <linux/sched/smt.h>

#include <asm/nospec-branch.h>
#include <asm/spec_ctrl.h>
#include <asm/cmdline.h>
#include <asm/bugs.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>
#include <asm/i387.h>
#include <asm/msr.h>
#include <asm/vmx.h>
#include <asm/paravirt.h>
#include <asm/alternative.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/intel-family.h>
#include <asm/cpu_device_id.h>
#include <asm/nospec-branch.h>
#include <asm/spec-ctrl.h>
#include <asm/e820.h>
#include <asm/hypervisor.h>

/* Control MDS CPU buffer clear before returning to user space */
bool mds_user_clear;
EXPORT_SYMBOL_GPL(mds_user_clear);
/* Control MDS CPU buffer clear before idling (halt, mwait) */
bool mds_idle_clear;
EXPORT_SYMBOL_GPL(mds_idle_clear);

static void ssb_init_cmd_line(void);

#ifdef CONFIG_X86_32
#ifndef CONFIG_XEN
static int __init no_halt(char *s)
{
	WARN_ONCE(1, "\"no-hlt\" is deprecated, please use \"idle=poll\"\n");
	boot_cpu_data.hlt_works_ok = 0;
	return 1;
}

__setup("no-hlt", no_halt);
#endif

static int __init no_387(char *s)
{
	boot_cpu_data.hard_math = 0;
	write_cr0(X86_CR0_TS | X86_CR0_EM | X86_CR0_MP | read_cr0());
	return 1;
}

__setup("no387", no_387);

static double __initdata x = 4195835.0;
static double __initdata y = 3145727.0;

/*
 * This used to check for exceptions..
 * However, it turns out that to support that,
 * the XMM trap handlers basically had to
 * be buggy. So let's have a correct XMM trap
 * handler, and forget about printing out
 * some status at boot.
 *
 * We should really only care about bugs here
 * anyway. Not features.
 */
static void __init check_fpu(void)
{
	s32 fdiv_bug;

	if (!boot_cpu_data.hard_math) {
#ifndef CONFIG_MATH_EMULATION
		printk(KERN_EMERG "No coprocessor found and no math emulation present.\n");
		printk(KERN_EMERG "Giving up.\n");
		for (;;) ;
#endif
		return;
	}

	kernel_fpu_begin();

	/*
	 * trap_init() enabled FXSR and company _before_ testing for FP
	 * problems here.
	 *
	 * Test for the divl bug..
	 */
	__asm__("fninit\n\t"
		"fldl %1\n\t"
		"fdivl %2\n\t"
		"fmull %2\n\t"
		"fldl %1\n\t"
		"fsubp %%st,%%st(1)\n\t"
		"fistpl %0\n\t"
		"fwait\n\t"
		"fninit"
		: "=m" (*&fdiv_bug)
		: "m" (*&x), "m" (*&y));

	kernel_fpu_end();

#ifndef CONFIG_XEN
	boot_cpu_data.fdiv_bug = fdiv_bug;
	if (boot_cpu_data.fdiv_bug)
		printk(KERN_WARNING "Hmm, FPU with FDIV bug.\n");
#endif
}

static void __init check_hlt(void)
{
#ifndef CONFIG_XEN
	if (boot_cpu_data.x86 >= 5 || paravirt_enabled())
		return;

	printk(KERN_INFO "Checking 'hlt' instruction... ");
	if (!boot_cpu_data.hlt_works_ok) {
		printk("disabled\n");
		return;
	}
	halt();
	halt();
	halt();
	halt();
	printk(KERN_CONT "OK.\n");
#endif
}

/*
 *	Most 386 processors have a bug where a POPAD can lock the
 *	machine even from user space.
 */

static void __init check_popad(void)
{
#ifndef CONFIG_X86_POPAD_OK
	int res, inp = (int) &res;

	printk(KERN_INFO "Checking for popad bug... ");
	__asm__ __volatile__(
	  "movl $12345678,%%eax; movl $0,%%edi; pusha; popa; movl (%%edx,%%edi),%%ecx "
	  : "=&a" (res)
	  : "d" (inp)
	  : "ecx", "edi");
	/*
	 * If this fails, it means that any user program may lock the
	 * CPU hard. Too bad.
	 */
	if (res != 12345678)
		printk(KERN_CONT "Buggy.\n");
	else
		printk(KERN_CONT "OK.\n");
#endif
}

/*
 * Check whether we are able to run this kernel safely on SMP.
 *
 * - In order to run on a i386, we need to be compiled for i386
 *   (for due to lack of "invlpg" and working WP on a i386)
 * - In order to run on anything without a TSC, we need to be
 *   compiled for a i486.
 */

static void __init check_config(void)
{
/*
 * We'd better not be a i386 if we're configured to use some
 * i486+ only features! (WP works in supervisor mode and the
 * new "invlpg" and "bswap" instructions)
 */
#if defined(CONFIG_X86_WP_WORKS_OK) || defined(CONFIG_X86_INVLPG) || \
	defined(CONFIG_X86_BSWAP)
	if (boot_cpu_data.x86 == 3)
		panic("Kernel requires i486+ for 'invlpg' and other features");
#endif
}
#endif /* CONFIG_X86_32 */

static void __init spectre_v1_select_mitigation(void);
static void __init spectre_v2_select_mitigation(void);
static void __init retbleed_select_mitigation(void);
void ssb_select_mitigation(void);
static void x86_amd_ssbd_disable(void);
static void __init l1tf_select_mitigation(void);
static void __init mds_select_mitigation(void);
static void __init md_clear_update_mitigation(void);
static void __init md_clear_select_mitigation(void);
static void __init taa_select_mitigation(void);
static void __init mmio_select_mitigation(void);
static void __init srbds_select_mitigation(void);
static void __init gds_select_mitigation(void);

/* The base value of the SPEC_CTRL MSR without task-specific bits set */
u64 x86_spec_ctrl_base;

/* The current value of the SPEC_CTRL MSR with task-specific bits set */
DEFINE_PER_CPU(u64, x86_spec_ctrl_current);
EXPORT_SYMBOL_GPL(x86_spec_ctrl_current);

static DEFINE_MUTEX(spec_ctrl_mutex);

/*
 * Keep track of the SPEC_CTRL MSR value for the current task, which may differ
 * from x86_spec_ctrl_base due to STIBP/SSB in __speculation_ctrl_update().
 */
void write_spec_ctrl_current(u64 val, bool force)
{
	if (this_cpu_read(x86_spec_ctrl_current) == val)
		return;

	this_cpu_write(x86_spec_ctrl_current, val);

	/*
	 * When KERNEL_IBRS this MSR is written on return-to-user, unless
	 * forced the update can be delayed until that time.
	 */
	if (force || !boot_cpu_has(X86_FEATURE_SPEC_CTRL))
		wrmsrl(MSR_IA32_SPEC_CTRL, val);
}

/*
 * The vendor and possibly platform specific bits which can be modified in
 * x86_spec_ctrl_base.
 */
static u64 x86_spec_ctrl_mask = ~SPEC_CTRL_IBRS;

/*
 * AMD specific MSR info for Speculative Store Bypass control.
 * x86_amd_ls_cfg_ssbd_mask is initialized in identify_boot_cpu().
 */
u64 x86_amd_ls_cfg_base;
u64 x86_amd_ls_cfg_ssbd_mask;

/* Controls CPU Fill buffer clear before KVM guest MMIO accesses */
bool mmio_stale_data_clear;
EXPORT_SYMBOL_GPL(mmio_stale_data_clear);

void __init check_bugs(void)
{
#ifdef CONFIG_X86_32
	/*
	 * Regardless of whether PCID is enumerated, the SDM says
	 * that it can't be enabled in 32-bit mode.
	 */
	setup_clear_cpu_cap(X86_FEATURE_PCID);
#endif

	identify_boot_cpu();

	/*
	 * identify_boot_cpu() initialized SMT support information, let the
	 * core code know.
	 */
	cpu_smt_check_topology_early();

#ifndef CONFIG_SMP
	printk(KERN_INFO "CPU: ");
	print_cpu_info(&boot_cpu_data);
#endif

	/*
	 * Read the SPEC_CTRL MSR to account for reserved bits which may
	 * have unknown values. AMD64_LS_CFG MSR is cached in the early AMD
	 * init code as it is not enumerated and depends on the family.
	 */
	if (boot_cpu_has(X86_FEATURE_IBRS))
		rdmsrl(MSR_IA32_SPEC_CTRL, x86_spec_ctrl_base);

	/* Select the proper CPU mitigations before patching alternatives: */
	spectre_v1_select_mitigation();
	spectre_v2_select_mitigation();
	/*
	 * retbleed_select_mitigation() relies on the state set by
	 * spectre_v2_select_mitigation(); specifically it wants to know about
	 * spectre_v2=ibrs.
	 */
	retbleed_select_mitigation();

	l1tf_select_mitigation();

	md_clear_select_mitigation();

	arch_smt_update();

	ssb_init_cmd_line();
	ssb_select_mitigation();
	srbds_select_mitigation();
	gds_select_mitigation();

#ifdef CONFIG_X86_32
	check_config();
	check_hlt();
	check_popad();
	init_utsname()->machine[1] =
		'0' + (boot_cpu_data.x86 > 6 ? 6 : boot_cpu_data.x86);
	alternative_instructions();

	/*
	 * kernel_fpu_begin/end() in check_fpu() relies on the patched
	 * alternative instructions.
	 */
	check_fpu();
#else /* CONFIG_X86_64 */

	alternative_instructions();
#ifndef CONFIG_XEN
	/*
	 * Make sure the first 2MB area is not mapped by huge pages
	 * There are typically fixed size MTRRs in there and overlapping
	 * MTRRs into large pages causes slow downs.
	 *
	 * Right now we don't do that with gbpages because there seems
	 * very little benefit for that case.
	 */
	if (!direct_gbpages)
		set_memory_4k((unsigned long)__va(0), 1);
#endif /* CONFIG_XEN */
#endif
}

#undef pr_fmt
#define pr_fmt(fmt)	"MDS: " fmt

enum mds_mitigations {
	MDS_MITIGATION_OFF,
	MDS_MITIGATION_FULL,
	MDS_MITIGATION_VMWERV,
};

/* Default mitigation for L1TF-affected CPUs */
static enum mds_mitigations mds_mitigation = MDS_MITIGATION_FULL;
static bool mds_nosmt = false;

static const char * const mds_strings[] = {
	[MDS_MITIGATION_OFF]	= "Vulnerable",
	[MDS_MITIGATION_FULL]	= "Mitigation: Clear CPU buffers",
	[MDS_MITIGATION_VMWERV]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
};

static bool x86_bug_mds;
static bool x86_bug_msbds_only;

static void __init mds_select_mitigation(void)
{
	if (!x86_bug_mds || cpu_mitigations_off()) {
		mds_mitigation = MDS_MITIGATION_OFF;
		return;
	}

	if (mds_mitigation == MDS_MITIGATION_FULL) {
		if (!boot_cpu_has(X86_FEATURE_MD_CLEAR))
			mds_mitigation = MDS_MITIGATION_VMWERV;

		mds_user_clear = true;

		if (!x86_bug_msbds_only &&
		    (mds_nosmt || cpu_mitigations_auto_nosmt()))
			cpu_smt_disable(false);
	}

	pr_info("%s\n", mds_strings[mds_mitigation]);
}

static int __init mds_cmdline(char *str)
{
	if (!x86_bug_mds)
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		mds_mitigation = MDS_MITIGATION_OFF;
	else if (!strcmp(str, "full"))
		mds_mitigation = MDS_MITIGATION_FULL;
	else if (!strcmp(str, "full,nosmt")) {
		mds_mitigation = MDS_MITIGATION_FULL;
		mds_nosmt = true;
	}

	return 0;
}
early_param("mds", mds_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)	"TAA: " fmt

enum taa_mitigations {
	TAA_MITIGATION_OFF,
	TAA_MITIGATION_UCODE_NEEDED,
	TAA_MITIGATION_VERW,
	TAA_MITIGATION_TSX_DISABLED,
};

/* Default mitigation for TAA-affected CPUs */
static enum taa_mitigations taa_mitigation = TAA_MITIGATION_VERW;
static bool taa_nosmt;

static const char * const taa_strings[] = {
	[TAA_MITIGATION_OFF]		= "Vulnerable",
	[TAA_MITIGATION_UCODE_NEEDED]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
	[TAA_MITIGATION_VERW]		= "Mitigation: Clear CPU buffers",
	[TAA_MITIGATION_TSX_DISABLED]	= "Mitigation: TSX disabled",
};

static bool x86_bug_taa;

u64 x86_read_arch_cap_msr(void)
{
	u64 ia32_cap = 0;

	if (boot_cpu_has(X86_FEATURE_ARCH_CAPABILITIES))
		rdmsrl(MSR_IA32_ARCH_CAPABILITIES, ia32_cap);

	return ia32_cap;
}

static void __init taa_select_mitigation(void)
{
	u64 ia32_cap;

	if (!x86_bug_taa) {
		taa_mitigation = TAA_MITIGATION_OFF;
		return;
	}

	/* TSX previously disabled by tsx=off */
	if (!boot_cpu_has(X86_FEATURE_RTM)) {
		taa_mitigation = TAA_MITIGATION_TSX_DISABLED;
		return;
	}

	if (cpu_mitigations_off()) {
		taa_mitigation = TAA_MITIGATION_OFF;
		return;
	}

	/*
	 * TAA mitigation via VERW is turned off if both
	 * tsx_async_abort=off and mds=off are specified.
	 */
	if (taa_mitigation == TAA_MITIGATION_OFF &&
	    mds_mitigation == MDS_MITIGATION_OFF)
		return;

	if (boot_cpu_has(X86_FEATURE_MD_CLEAR))
		taa_mitigation = TAA_MITIGATION_VERW;
	else
		taa_mitigation = TAA_MITIGATION_UCODE_NEEDED;

	/*
	 * VERW doesn't clear the CPU buffers when MD_CLEAR=1 and MDS_NO=1.
	 * A microcode update fixes this behavior to clear CPU buffers. It also
	 * adds support for MSR_IA32_TSX_CTRL which is enumerated by the
	 * ARCH_CAP_TSX_CTRL_MSR bit.
	 *
	 * On MDS_NO=1 CPUs if ARCH_CAP_TSX_CTRL_MSR is not set, microcode
	 * update is required.
	 */
	ia32_cap = x86_read_arch_cap_msr();
	if ( (ia32_cap & ARCH_CAP_MDS_NO) &&
	    !(ia32_cap & ARCH_CAP_TSX_CTRL_MSR))
		taa_mitigation = TAA_MITIGATION_UCODE_NEEDED;

	/*
	 * TSX is enabled, select alternate mitigation for TAA which is
	 * the same as MDS. Enable MDS static branch to clear CPU buffers.
	 *
	 * For guests that can't determine whether the correct microcode is
	 * present on host, enable the mitigation for UCODE_NEEDED as well.
	 */
	mds_user_clear = true;

	if (taa_nosmt || cpu_mitigations_auto_nosmt())
		cpu_smt_disable(false);
}

static int __init tsx_async_abort_parse_cmdline(char *str)
{
	if (!x86_bug_taa)
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off")) {
		taa_mitigation = TAA_MITIGATION_OFF;
	} else if (!strcmp(str, "full")) {
		taa_mitigation = TAA_MITIGATION_VERW;
	} else if (!strcmp(str, "full,nosmt")) {
		taa_mitigation = TAA_MITIGATION_VERW;
		taa_nosmt = true;
	}

	return 0;
}
early_param("tsx_async_abort", tsx_async_abort_parse_cmdline);

static bool x86_bug_mmio;

bool x86_bug_gds = false;

#undef pr_fmt
#define pr_fmt(fmt)	"MMIO Stale Data: " fmt

enum mmio_mitigations {
	MMIO_MITIGATION_OFF,
	MMIO_MITIGATION_UCODE_NEEDED,
	MMIO_MITIGATION_VERW,
};

/* Default mitigation for Processor MMIO Stale Data vulnerabilities */
static enum mmio_mitigations mmio_mitigation = MMIO_MITIGATION_VERW;
static bool mmio_nosmt = false;

static const char * const mmio_strings[] = {
	[MMIO_MITIGATION_OFF]		= "Vulnerable",
	[MMIO_MITIGATION_UCODE_NEEDED]	= "Vulnerable: Clear CPU buffers attempted, no microcode",
	[MMIO_MITIGATION_VERW]		= "Mitigation: Clear CPU buffers",
};

static void __init mmio_select_mitigation(void)
{
	u64 ia32_cap;

	if (!x86_bug_mmio || cpu_mitigations_off()) {
		mmio_mitigation = MMIO_MITIGATION_OFF;
		return;
	}

	if (mmio_mitigation == MMIO_MITIGATION_OFF)
		return;

	ia32_cap = x86_read_arch_cap_msr();

	/*
	 * Enable CPU buffer clear mitigation for host and VMM, if also affected
	 * by MDS or TAA. Otherwise, enable mitigation for VMM only.
	 */
	if (x86_bug_mds || (x86_bug_taa && boot_cpu_has(X86_FEATURE_RTM)))
		mds_user_clear = true;
	else
		mmio_stale_data_clear = true;

	/*
	 * If Processor-MMIO-Stale-Data bug is present and Fill Buffer data can
	 * be propagated to uncore buffers, clearing the Fill buffers on idle
	 * is required irrespective of SMT state.
	 */
	if (!(ia32_cap & ARCH_CAP_FBSDP_NO))
		mds_idle_clear = true;

	/*
	 * Check if the system has the right microcode.
	 *
	 * CPU Fill buffer clear mitigation is enumerated by either an explicit
	 * FB_CLEAR or by the presence of both MD_CLEAR and L1D_FLUSH on MDS
	 * affected systems.
	 */
	if ((ia32_cap & ARCH_CAP_FB_CLEAR) ||
	    (boot_cpu_has(X86_FEATURE_MD_CLEAR) &&
	     boot_cpu_has(X86_FEATURE_FLUSH_L1D) &&
	     !(ia32_cap & ARCH_CAP_MDS_NO)))
		mmio_mitigation = MMIO_MITIGATION_VERW;
	else
		mmio_mitigation = MMIO_MITIGATION_UCODE_NEEDED;

	if (mmio_nosmt || cpu_mitigations_auto_nosmt())
		cpu_smt_disable(false);
}

static int __init mmio_stale_data_parse_cmdline(char *str)
{
	if (!x86_bug_mmio)
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off")) {
		mmio_mitigation = MMIO_MITIGATION_OFF;
	} else if (!strcmp(str, "full")) {
		mmio_mitigation = MMIO_MITIGATION_VERW;
	} else if (!strcmp(str, "full,nosmt")) {
		mmio_mitigation = MMIO_MITIGATION_VERW;
		mmio_nosmt = true;
	}

	return 0;
}
early_param("mmio_stale_data", mmio_stale_data_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)     "" fmt

static void __init md_clear_update_mitigation(void)
{
	if (cpu_mitigations_off())
		return;

	if (!mds_user_clear)
		goto out;

	/*
	 * mds_user_clear is now enabled. Update MDS, TAA and MMIO Stale Data
	 * mitigation, if necessary.
	 */
	if (mds_mitigation == MDS_MITIGATION_OFF &&
	    x86_bug_mds) {
		mds_mitigation = MDS_MITIGATION_FULL;
		mds_select_mitigation();
	}
	if (taa_mitigation == TAA_MITIGATION_OFF && x86_bug_taa) {
		taa_mitigation = TAA_MITIGATION_VERW;
		taa_select_mitigation();
	}
	if (mmio_mitigation == MMIO_MITIGATION_OFF && x86_bug_mmio) {
		mmio_mitigation = MMIO_MITIGATION_VERW;
		mmio_select_mitigation();
	}
out:
	if (x86_bug_mds)
		pr_info("MDS: %s\n", mds_strings[mds_mitigation]);
	if (x86_bug_taa)
		pr_info("TAA: %s\n", taa_strings[taa_mitigation]);
	if (x86_bug_mmio)
		pr_info("MMIO Stale Data: %s\n", mmio_strings[mmio_mitigation]);
}

static void __init md_clear_select_mitigation(void)
{
	mds_select_mitigation();
	taa_select_mitigation();
	mmio_select_mitigation();

	/*
	 * As MDS, TAA and MMIO Stale Data mitigations are inter-related, update
	 * and print their mitigation after MDS, TAA and MMIO Stale Data
	 * mitigation selection is done.
	 */
	md_clear_update_mitigation();
}

static bool x86_bug_srbds;

#undef pr_fmt
#define pr_fmt(fmt)	"GDS: " fmt

enum gds_mitigations {
	GDS_MITIGATION_OFF,
	GDS_MITIGATION_UCODE_NEEDED,
	GDS_MITIGATION_FULL,
	GDS_MITIGATION_FULL_LOCKED,
	GDS_MITIGATION_HYPERVISOR,
};

static enum gds_mitigations gds_mitigation = GDS_MITIGATION_FULL;

static const char * const gds_strings[] = {
	[GDS_MITIGATION_OFF]		= "Vulnerable",
	[GDS_MITIGATION_UCODE_NEEDED]	= "Vulnerable: No microcode",
	[GDS_MITIGATION_FULL]		= "Mitigation: Microcode",
	[GDS_MITIGATION_FULL_LOCKED]	= "Mitigation: Microcode (locked)",
	[GDS_MITIGATION_HYPERVISOR]	= "Unknown: Dependent on hypervisor status",
};

void update_gds_msr(void)
{
	u64 mcu_ctrl_after;
	u64 mcu_ctrl;

	switch (gds_mitigation) {
	case GDS_MITIGATION_OFF:
		rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
		mcu_ctrl |= GDS_MITG_DIS;
		break;
	case GDS_MITIGATION_FULL_LOCKED:
		/*
		 * The LOCKED state comes from the boot CPU. APs might not have
		 * the same state. Make sure the mitigation is enabled on all
		 * CPUs.
		 */
	case GDS_MITIGATION_FULL:
		rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
		mcu_ctrl &= ~GDS_MITG_DIS;
		break;
	case GDS_MITIGATION_UCODE_NEEDED:
	case GDS_MITIGATION_HYPERVISOR:
		return;
	};

	wrmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);

	/*
	 * Check to make sure that the WRMSR value was not ignored. Writes to
	 * GDS_MITG_DIS will be ignored if this processor is locked but the boot
	 * processor was not.
	 */
	rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl_after);
	WARN_ON_ONCE(mcu_ctrl != mcu_ctrl_after);
}

static void __init gds_select_mitigation(void)
{
	u64 mcu_ctrl;

	if (!x86_bug_gds)
		return;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		gds_mitigation = GDS_MITIGATION_HYPERVISOR;
		goto out;
	}

	if (cpu_mitigations_off())
		gds_mitigation = GDS_MITIGATION_OFF;
	/* Will verify below that mitigation _can_ be disabled */

	/* No microcode */
	if (!(x86_read_arch_cap_msr() & ARCH_CAP_GDS_CTRL)) {
		gds_mitigation = GDS_MITIGATION_UCODE_NEEDED;
		goto out;
	}

	rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
	if (mcu_ctrl & GDS_MITG_LOCKED) {
		if (gds_mitigation == GDS_MITIGATION_OFF)
			pr_warn("Mitigation locked. Disable failed.\n");

		/*
		 * The mitigation is selected from the boot CPU. All other CPUs
		 * _should_ have the same state. If the boot CPU isn't locked
		 * but others are then update_gds_msr() will WARN() of the state
		 * mismatch. If the boot CPU is locked update_gds_msr() will
		 * ensure the other CPUs have the mitigation enabled.
		 */
		gds_mitigation = GDS_MITIGATION_FULL_LOCKED;
	}

	update_gds_msr();
out:
	pr_info("%s\n", gds_strings[gds_mitigation]);
}

static int __init gds_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!x86_bug_gds)
		return 0;

	if (!strcmp(str, "off"))
		gds_mitigation = GDS_MITIGATION_OFF;

	return 0;
}
early_param("gather_data_sampling", gds_parse_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)	"SRBDS: " fmt

enum srbds_mitigations {
	SRBDS_MITIGATION_OFF,
	SRBDS_MITIGATION_UCODE_NEEDED,
	SRBDS_MITIGATION_FULL,
	SRBDS_MITIGATION_TSX_OFF,
	SRBDS_MITIGATION_HYPERVISOR,
};

static enum srbds_mitigations srbds_mitigation = SRBDS_MITIGATION_FULL;

static const char * const srbds_strings[] = {
	[SRBDS_MITIGATION_OFF]		= "Vulnerable",
	[SRBDS_MITIGATION_UCODE_NEEDED]	= "Vulnerable: No microcode",
	[SRBDS_MITIGATION_FULL]		= "Mitigation: Microcode",
	[SRBDS_MITIGATION_TSX_OFF]	= "Mitigation: TSX disabled",
	[SRBDS_MITIGATION_HYPERVISOR]	= "Unknown: Dependent on hypervisor status",
};

static bool srbds_off;

void update_srbds_msr(void)
{
	u64 mcu_ctrl;

	if (!x86_bug_srbds)
		return;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return;

	if (srbds_mitigation == SRBDS_MITIGATION_UCODE_NEEDED)
		return;

	rdmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);

	switch (srbds_mitigation) {
	case SRBDS_MITIGATION_OFF:
	case SRBDS_MITIGATION_TSX_OFF:
		mcu_ctrl |= RNGDS_MITG_DIS;
		break;
	case SRBDS_MITIGATION_FULL:
		mcu_ctrl &= ~RNGDS_MITG_DIS;
		break;
	default:
		break;
	}

	wrmsrl(MSR_IA32_MCU_OPT_CTRL, mcu_ctrl);
}

static void __init srbds_select_mitigation(void)
{
	u64 ia32_cap;

	if (!x86_bug_srbds)
		return;

	/*
	 * Check to see if this is one of the MDS_NO systems supporting TSX that
	 * are only exposed to SRBDS when TSX is enabled or when CPU is affected
	 * by Processor MMIO Stale Data vulnerability.
	 */
	ia32_cap = x86_read_arch_cap_msr();
	if ((ia32_cap & ARCH_CAP_MDS_NO) && !boot_cpu_has(X86_FEATURE_RTM) &&
	    !x86_bug_mmio)
		srbds_mitigation = SRBDS_MITIGATION_TSX_OFF;
	else if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		srbds_mitigation = SRBDS_MITIGATION_HYPERVISOR;
	else if (!boot_cpu_has(X86_FEATURE_SRBDS_CTRL))
		srbds_mitigation = SRBDS_MITIGATION_UCODE_NEEDED;
	else if (cpu_mitigations_off() || srbds_off)
		srbds_mitigation = SRBDS_MITIGATION_OFF;

	update_srbds_msr();
	pr_info("%s\n", srbds_strings[srbds_mitigation]);
}

static int __init srbds_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (!x86_bug_srbds)
		return 0;

	srbds_off = !strcmp(str, "off");
	return 0;
}
early_param("srbds", srbds_parse_cmdline);

/* The kernel command line selection */
enum spectre_v2_mitigation_cmd {
	SPECTRE_V2_CMD_NONE,
	SPECTRE_V2_CMD_AUTO,
	SPECTRE_V2_CMD_FORCE,
	SPECTRE_V2_CMD_RETPOLINE,
	SPECTRE_V2_CMD_RETPOLINE_GENERIC,
	SPECTRE_V2_CMD_RETPOLINE_LFENCE,
	SPECTRE_V2_CMD_IBRS,
};

static const char *spectre_v2_strings[] = {
	[SPECTRE_V2_NONE]			= "Vulnerable",
	[SPECTRE_V2_RETPOLINE_MINIMAL]		= "Vulnerable: Minimal generic ASM retpoline",
	[SPECTRE_V2_RETPOLINE_MINIMAL_AMD]	= "Vulnerable: Minimal AMD ASM retpoline",
	[SPECTRE_V2_RETPOLINE]			= "Mitigation: Retpolines",
	[SPECTRE_V2_LFENCE]			= "Mitigation: LFENCE",
	[SPECTRE_V2_IBRS]			= "Mitigation: IBRS",
};

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V1 : " fmt

enum spectre_v1_mitigation {
	SPECTRE_V1_MITIGATION_NONE,
	SPECTRE_V1_MITIGATION_AUTO,
};

static enum spectre_v1_mitigation spectre_v1_mitigation =
	SPECTRE_V1_MITIGATION_AUTO;

static const char * const spectre_v1_strings[] = {
	[SPECTRE_V1_MITIGATION_NONE] = "Vulnerable: __user pointer sanitization and usercopy barriers only; no swapgs barriers",
	[SPECTRE_V1_MITIGATION_AUTO] = "Mitigation: usercopy/swapgs barriers and __user pointer sanitization",
};

static bool is_swapgs_serializing(void)
{
	/*
	 * Technically, swapgs isn't serializing on AMD (despite it previously
	 * being documented as such in the APM).  But according to AMD, %gs is
	 * updated non-speculatively, and the issuing of %gs-relative memory
	 * operands will be blocked until the %gs update completes, which is
	 * good enough for our purposes.
	 */
	return boot_cpu_data.x86_vendor == X86_VENDOR_AMD;
}

static bool x86_bug_spectre_v1, x86_bug_spectre_v2, x86_bug_meltdown;

/*
 * Does SMAP provide full mitigation against speculative kernel access to
 * userspace?
 */
static bool smap_works_speculatively(void)
{
	/* SMAP feature bit is not available */
#if 0
	if (!boot_cpu_has(X86_FEATURE_SMAP))
		return false;
#endif

	/*
	 * On CPUs which are vulnerable to Meltdown, SMAP does not
	 * prevent speculative access to user data in the L1 cache.
	 * Consider SMAP to be non-functional as a mitigation on these
	 * CPUs.
	 */
	if (x86_bug_meltdown)
		return false;

	return true;
}

static void __init spectre_v1_select_mitigation(void)
{
	if (!x86_bug_spectre_v1 || cpu_mitigations_off()) {
		spectre_v1_mitigation = SPECTRE_V1_MITIGATION_NONE;
		return;
	}

	if (spectre_v1_mitigation == SPECTRE_V1_MITIGATION_AUTO) {
		/*
		 * With Spectre v1, a user can speculatively control either
		 * path of a conditional swapgs with a user-controlled GS
		 * value.  The mitigation is to add lfences to both code paths.
		 *
		 * If FSGSBASE is enabled, the user can put a kernel address in
		 * GS, in which case SMAP provides no protection.
		 *
		 * [ NOTE: Don't check for X86_FEATURE_FSGSBASE until the
		 *	   FSGSBASE enablement patches have been merged. ]
		 *
		 * If FSGSBASE is disabled, the user can only put a user space
		 * address in GS.  That makes an attack harder, but still
		 * possible if there's no SMAP protection.
		 */
		if (!smap_works_speculatively()) {
			/*
			 * Mitigation can be provided from SWAPGS itself or
			 * PTI as the CR3 write in the Meltdown mitigation
			 * is serializing.
			 *
			 * If neither is there, mitigate with an LFENCE.
			 */
			if (!is_swapgs_serializing() && !boot_cpu_has(X86_FEATURE_KAISER))
				setup_force_cpu_cap(X86_FEATURE_FENCE_SWAPGS_USER);

			/*
			 * Enable lfences in the kernel entry (non-swapgs)
			 * paths, to prevent user entry from speculatively
			 * skipping swapgs.
			 */
			setup_force_cpu_cap(X86_FEATURE_FENCE_SWAPGS_KERNEL);
		}
	}

	pr_info("%s\n", spectre_v1_strings[spectre_v1_mitigation]);
}

static int __init nospectre_v1_cmdline(char *str)
{
	spectre_v1_mitigation = SPECTRE_V1_MITIGATION_NONE;
	return 0;
}
early_param("nospectre_v1", nospectre_v1_cmdline);


#undef pr_fmt
#define pr_fmt(fmt)     "RETBleed: " fmt

enum retbleed_mitigation {
	RETBLEED_MITIGATION_NONE,
	RETBLEED_MITIGATION_UNRET,
	RETBLEED_MITIGATION_IBRS,
	RETBLEED_MITIGATION_EIBRS,
};

enum retbleed_mitigation_cmd {
	RETBLEED_CMD_OFF,
	RETBLEED_CMD_AUTO,
	RETBLEED_CMD_UNRET,
};

const char * const retbleed_strings[] = {
	[RETBLEED_MITIGATION_NONE]	= "Vulnerable",
	[RETBLEED_MITIGATION_UNRET]	= "Mitigation: untrained return thunk",
	[RETBLEED_MITIGATION_IBRS]	= "Mitigation: IBRS",
	[RETBLEED_MITIGATION_EIBRS]	= "Mitigation: Enhanced IBRS",
};

static enum retbleed_mitigation retbleed_mitigation = RETBLEED_MITIGATION_NONE;
static enum retbleed_mitigation_cmd retbleed_cmd = RETBLEED_CMD_AUTO;

static int retbleed_nosmt = false;

static int __init retbleed_parse_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	while (str) {
		char *next = strchr(str, ',');
		if (next) {
			*next = 0;
			next++;
		}

		if (!strcmp(str, "off")) {
			retbleed_cmd = RETBLEED_CMD_OFF;;
		} else if (!strcmp(str, "auto")) {
			retbleed_cmd = RETBLEED_CMD_AUTO;
		} else if (!strcmp(str, "unret")) {
			retbleed_cmd = RETBLEED_CMD_UNRET;;
		} else if (!strcmp(str, "nosmt")) {
			retbleed_nosmt = true;
		} else {
			pr_err("Ignoring unknown retbleed option (%s).", str);
		}

		str = next;
	}

	return 0;
}
early_param("retbleed", retbleed_parse_cmdline);

#define RETBLEED_UNTRAIN_MSG "WARNING: BTB untrained return thunk mitigation is only effective on AMD/Hygon!\n"
#define RETBLEED_COMPILER_MSG "WARNING: kernel not compiled with RETPOLINE or -mfunction-return capable compiler!\n"
#define RETBLEED_INTEL_MSG "WARNING: Spectre v2 mitigation leaves CPU vulnerable to RETBleed attacks, data leaks possible!\n"

static enum spectre_v2_mitigation spectre_v2_enabled = SPECTRE_V2_NONE;

static void __init retbleed_select_mitigation(void)
{
	if (!boot_cpu_has_bug(X86_BUG_RETBLEED))
		return;

	switch (retbleed_cmd) {
	case RETBLEED_CMD_OFF:
		return;

	case RETBLEED_CMD_UNRET:
		retbleed_mitigation = RETBLEED_MITIGATION_UNRET;
		break;

	case RETBLEED_CMD_AUTO:
	default:
		if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
			retbleed_mitigation = RETBLEED_MITIGATION_UNRET;

		/*
		 * The Intel mitigation (IBRS or eIBRS) was already selected in
		 * spectre_v2_select_mitigation().  'retbleed_mitigation' will
		 * be set accordingly below.
		 */

		break;
	}

	switch (retbleed_mitigation) {
	case RETBLEED_MITIGATION_UNRET:
		if (retbleed_nosmt || cpu_mitigations_auto_nosmt())
			cpu_smt_disable(false);

		if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
			pr_err(RETBLEED_UNTRAIN_MSG);
		break;
	default:
		break;
	}

	/*
	 * Let IBRS trump all on Intel without affecting the effects of the
	 * retbleed= cmdline option.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
		switch (spectre_v2_enabled) {
		case SPECTRE_V2_IBRS:
			retbleed_mitigation = RETBLEED_MITIGATION_IBRS;
			break;
		default:
			pr_err(RETBLEED_INTEL_MSG);
		}
	}

	pr_info("%s\n", retbleed_strings[retbleed_mitigation]);
}

#undef pr_fmt
#define pr_fmt(fmt)     "Spectre V2 mitigation: " fmt

#ifdef RETPOLINE
static bool spectre_v2_bad_module;

bool retpoline_module_ok(bool has_retpoline)
{
	if (spectre_v2_enabled == SPECTRE_V2_NONE || has_retpoline)
		return true;

	pr_err("System may be vunerable to spectre v2\n");
	spectre_v2_bad_module = true;
	return false;
}

static inline const char *spectre_v2_module_string(void)
{
	return spectre_v2_bad_module ? " - vulnerable module loaded" : "";
}
#else
static inline const char *spectre_v2_module_string(void) { return ""; }
#endif

#define NO_SPECULATION		BIT(0)
#define NO_MELTDOWN		BIT(1)
#define NO_SSB			BIT(2)
#define NO_L1TF			BIT(3)
#define NO_MDS			BIT(4)
#define MSBDS_ONLY		BIT(5)
#define NO_ITLB_MULTIHIT	BIT(7)

#define VULNWL(vendor, family, model, whitelist)	\
	X86_MATCH_VENDOR_FAM_MODEL(vendor, family, model, whitelist)

#define VULNWL_INTEL(model, whitelist)		\
	VULNWL(INTEL, 6, INTEL_FAM6_##model, whitelist)

#define VULNWL_AMD(family, whitelist)		\
	VULNWL(AMD, family, X86_MODEL_ANY, whitelist)

static const __initconst struct x86_cpu_id cpu_vuln_whitelist[] = {
	VULNWL(ANY,	4, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(CENTAUR,	5, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(INTEL,	5, X86_MODEL_ANY,	NO_SPECULATION),
	VULNWL(NSC,	5, X86_MODEL_ANY,	NO_SPECULATION),

	/* Intel Family 6 */
	VULNWL_INTEL(ATOM_SALTWELL,		NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SALTWELL_TABLET,	NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SALTWELL_MID,		NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_BONNELL,		NO_SPECULATION | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_BONNELL_MID,		NO_SPECULATION | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(ATOM_SILVERMONT,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SILVERMONT_X,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_SILVERMONT_MID,	NO_SSB | NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_AIRMONT,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(XEON_PHI_KNL,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(XEON_PHI_KNM,		NO_SSB | NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(CORE_YONAH,		NO_SSB),

	VULNWL_INTEL(ATOM_AIRMONT_MID,		NO_L1TF | MSBDS_ONLY | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_GOLDMONT,		NO_MDS | NO_L1TF | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_GOLDMONT_X,		NO_MDS | NO_L1TF | NO_ITLB_MULTIHIT),
	VULNWL_INTEL(ATOM_GOLDMONT_PLUS,	NO_MDS | NO_L1TF | NO_ITLB_MULTIHIT),

	VULNWL_INTEL(ATOM_TREMONT_X,		NO_ITLB_MULTIHIT),

	/* AMD Family 0xf - 0x12 */
	VULNWL_AMD(0x0f,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_ITLB_MULTIHIT),
	VULNWL_AMD(0x10,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_ITLB_MULTIHIT),
	VULNWL_AMD(0x11,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_ITLB_MULTIHIT),
	VULNWL_AMD(0x12,	NO_MELTDOWN | NO_SSB | NO_L1TF | NO_MDS | NO_ITLB_MULTIHIT),

	/* FAMILY_ANY must be last, otherwise 0x0f - 0x12 matches won't work */
	VULNWL_AMD(X86_FAMILY_ANY,	NO_MELTDOWN | NO_L1TF | NO_MDS | NO_ITLB_MULTIHIT),
	{}
};

#define VULNBL_INTEL_STEPPINGS(model, steppings, issues)		   \
	X86_MATCH_VENDOR_FAM_MODEL_STEPPINGS_FEATURE(INTEL, 6,		   \
					    INTEL_FAM6_##model, steppings, \
					    X86_FEATURE_ANY, issues)

#define SRBDS		BIT(0)
/* CPU is affected by RETbleed, speculating where you would not expect it */
/* CPU is affected by X86_BUG_MMIO_STALE_DATA */
#define MMIO		BIT(1)
#define RETBLEED	BIT(2)
/* CPU is affected by GDS */
#define GDS		BIT(5)

static const struct x86_cpu_id cpu_vuln_blacklist[] __initconst = {
	VULNBL_INTEL_STEPPINGS(IVYBRIDGE,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_CORE,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_ULT,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_GT3E,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(HASWELL_X,		BIT(2) | BIT(4),		MMIO),
	VULNBL_INTEL_STEPPINGS(BROADWELL_XEON_D,	X86_STEPPINGS(0x3, 0x5),	MMIO),
	VULNBL_INTEL_STEPPINGS(BROADWELL_GT3E,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(BROADWELL_X,	X86_STEPPING_ANY,		MMIO),
	VULNBL_INTEL_STEPPINGS(BROADWELL_CORE,	X86_STEPPING_ANY,		SRBDS),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_MOBILE,	X86_STEPPINGS(0x3, 0x3),	SRBDS | MMIO),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_MOBILE,	X86_STEPPING_ANY,		SRBDS | RETBLEED),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_X,	BIT(3) | BIT(4) | BIT(6) |
						BIT(7) | BIT(0xB),              MMIO),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_DESKTOP,	X86_STEPPINGS(0x3, 0x3),	SRBDS | MMIO),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_DESKTOP,	X86_STEPPING_ANY,		SRBDS | RETBLEED),
	VULNBL_INTEL_STEPPINGS(SKYLAKE_X,	X86_STEPPING_ANY,		RETBLEED | GDS),

	VULNBL_INTEL_STEPPINGS(KABYLAKE_MOBILE,	X86_STEPPINGS(0x9, 0xC),	SRBDS | MMIO | GDS),
	VULNBL_INTEL_STEPPINGS(KABYLAKE_MOBILE,	X86_STEPPINGS(0x0, 0x8),	SRBDS | GDS),
	VULNBL_INTEL_STEPPINGS(KABYLAKE_DESKTOP, X86_STEPPINGS(0x9, 0xD),	SRBDS | MMIO | GDS),
	VULNBL_INTEL_STEPPINGS(KABYLAKE_DESKTOP, X86_STEPPINGS(0x0, 0x8),	SRBDS | GDS),
	VULNBL_INTEL_STEPPINGS(ATOM_TREMONT_X,	X86_STEPPING_ANY,		MMIO),
	{}
};

static bool __init cpu_matches(const struct x86_cpu_id *table, unsigned long which)
{
	const struct x86_cpu_id *m = x86_match_cpu_stp(table);

	return m && !!(m->driver_data & which);
}

static bool x86_bug_spec_store_bypass;
static bool x86_bug_l1tf;
static bool x86_bug_itlb_multihit;
static bool x86_bug_retbleed;

bool arch_has_pfn_modify_check(void)
{
	return x86_bug_l1tf;
}
EXPORT_SYMBOL_GPL(arch_has_pfn_modify_check);

bool has_bug_itlb_multihit(void)
{
	return x86_bug_itlb_multihit;
}
EXPORT_SYMBOL_GPL(has_bug_itlb_multihit);

static bool arch_cap_mmio_immune(u64 ia32_cap)
{
	return (ia32_cap & ARCH_CAP_FBSDP_NO &&
		ia32_cap & ARCH_CAP_PSDP_NO &&
		ia32_cap & ARCH_CAP_SBDR_SSDP_NO);
}

void setup_force_cpu_bugs(unsigned long __unused)
{
	u64 ia32_cap = x86_read_arch_cap_msr();

	x86_bug_spectre_v1 = true;
	x86_bug_spectre_v2 = true;
	x86_bug_l1tf = true;

	/* Set ITLB_MULTIHIT bug if cpu is not in the whitelist and not mitigated */
	if (!cpu_matches(cpu_vuln_whitelist, NO_ITLB_MULTIHIT) &&
	    !(ia32_cap & ARCH_CAP_PSCHANGE_MC_NO))
		x86_bug_itlb_multihit = true;

	if (!cpu_matches(cpu_vuln_whitelist, NO_SSB))
		x86_bug_spec_store_bypass = true;

	if (!cpu_matches(cpu_vuln_whitelist, NO_MDS) &&
	    !(ia32_cap & ARCH_CAP_MDS_NO)) {
		x86_bug_mds = true;
		if (cpu_matches(cpu_vuln_whitelist, MSBDS_ONLY))
			x86_bug_msbds_only = true;
	}

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		x86_bug_meltdown = false;
	else
		x86_bug_meltdown = true;

	if (cpu_matches(cpu_vuln_whitelist, NO_L1TF))
		x86_bug_l1tf = false;

	/*
	 * When the CPU is not mitigated for TAA (TAA_NO=0) set TAA bug when:
	 *	- TSX is supported or
	 *	- TSX_CTRL is present
	 *
	 * TSX_CTRL check is needed for cases when TSX could be disabled before
	 * the kernel boot e.g. kexec.
	 * TSX_CTRL check alone is not sufficient for cases when the microcode
	 * update is not present or running as guest that don't get TSX_CTRL.
	 */
	if (!(ia32_cap & ARCH_CAP_TAA_NO) &&
	    (boot_cpu_has(X86_FEATURE_RTM) ||
	     (ia32_cap & ARCH_CAP_TSX_CTRL_MSR)))
		x86_bug_taa = true;

	/*
	 * SRBDS affects CPUs which support RDRAND or RDSEED and are listed
	 * in the vulnerability blacklist.
	 */
	if (boot_cpu_has(X86_FEATURE_RDRAND) &&
	    cpu_matches(cpu_vuln_blacklist, SRBDS))
		    x86_bug_srbds = true;

	if ((cpu_matches(cpu_vuln_blacklist, RETBLEED) || (ia32_cap & ARCH_CAP_RSBA)))
		x86_bug_retbleed = true;

	/*
	 * Processor MMIO Stale Data bug enumeration
	 *
	 * Affected CPU list is generally enough to enumerate the vulnerability,
	 * but for virtualization case check for ARCH_CAP MSR bits also, VMM may
	 * not want the guest to enumerate the bug.
	 */
	if (cpu_matches(cpu_vuln_blacklist, MMIO) &&
	    !arch_cap_mmio_immune(ia32_cap))
		x86_bug_mmio = true;

	/*
	 * Check if CPU is vulnerable to GDS. If running in a virtual machine on
	 * an affected processor, the VMM may have disabled the use of GATHER by
	 * disabling AVX2. The only way to do this in HW is to clear XCR0[2],
	 * which means that AVX will be disabled.
	 */
	if (cpu_matches(cpu_vuln_blacklist, GDS) && !(ia32_cap & ARCH_CAP_GDS_NO) &&
	    boot_cpu_has(X86_FEATURE_AVX))
		x86_bug_gds = true;
}

static void __init spec2_print_if_insecure(const char *reason)
{
	if (x86_bug_spectre_v2)
		pr_info("%s\n", reason);
}

static void __init spec2_print_if_secure(const char *reason)
{
	if (!x86_bug_spectre_v2)
		pr_info("%s\n", reason);
}

static inline bool retp_compiler(void)
{
#ifdef RETPOLINE
	return true;
#else
	return false;
#endif
}

static inline bool match_option(const char *arg, int arglen, const char *opt)
{
	int len = strlen(opt);

	return len == arglen && !strncmp(arg, opt, len);
}

static inline bool spectre_v2_in_ibrs_mode(enum spectre_v2_mitigation mode)
{
	return mode == SPECTRE_V2_IBRS;
}

static enum spectre_v2_mitigation_cmd spectre_v2_cmd;

static enum spectre_v2_mitigation_cmd __init spectre_v2_parse_cmdline(void)
{
	enum spectre_v2_mitigation_cmd cmd = SPECTRE_V2_CMD_AUTO;
	char arg[20];
	int ret;

	if (cmdline_find_option_bool(boot_command_line, "nospectre_v2") ||
	    cpu_mitigations_off()) {
		cmd = SPECTRE_V2_CMD_NONE;
		goto done;
	}

	ret = cmdline_find_option(boot_command_line, "spectre_v2", arg, sizeof(arg));
	if (ret > 0)  {
		if (match_option(arg, ret, "off")) {
			spec2_print_if_insecure("disabled on command line.");
			cmd = SPECTRE_V2_CMD_NONE;
		} else if (match_option(arg, ret, "on")) {
			spec2_print_if_secure("force enabled on command line.");
			cmd = SPECTRE_V2_CMD_FORCE;
		} else if (match_option(arg, ret, "retpoline")) {
			spec2_print_if_insecure("retpoline selected on command line.");
			cmd = SPECTRE_V2_CMD_RETPOLINE;
		} else if (match_option(arg, ret, "retpoline,amd")) {
			spec2_print_if_insecure("LFENCE retpoline selected on command line.");
			cmd = SPECTRE_V2_CMD_RETPOLINE_LFENCE;
		} else if (match_option(arg, ret, "retpoline,lfence")) {
			spec2_print_if_insecure("LFENCE retpoline selected on command line.");
			cmd = SPECTRE_V2_CMD_RETPOLINE_LFENCE;
		} else if (match_option(arg, ret, "retpoline,generic")) {
			spec2_print_if_insecure("generic retpoline selected on command line.");
			cmd = SPECTRE_V2_CMD_RETPOLINE_GENERIC;
		} else if (match_option(arg, ret, "auto")) {
			cmd = SPECTRE_V2_CMD_AUTO;
		} else if (match_option(arg, ret, "ibrs")) {
			cmd = SPECTRE_V2_CMD_IBRS;
		}
	}

	if (cmd == SPECTRE_V2_CMD_IBRS && boot_cpu_data.x86_vendor != X86_VENDOR_INTEL) {
		pr_err("ibrs selected but not Intel CPU. Switching to AUTO select\n");
		return SPECTRE_V2_CMD_AUTO;
	}

	if (cmd == SPECTRE_V2_CMD_IBRS && !boot_cpu_has(X86_FEATURE_IBRS)) {
		pr_err("ibrs selected but CPU doesn't have IBRS. Switching to AUTO select\n");
		return SPECTRE_V2_CMD_AUTO;
	}

	if ((cmd == SPECTRE_V2_CMD_RETPOLINE_LFENCE) &&
	    !boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
		pr_err("retpoline,lfence selected, but CPU doesn't have a serializing LFENCE. Switching to AUTO select\n");
		cmd = SPECTRE_V2_CMD_AUTO;
	}

done:
	if (cmd == SPECTRE_V2_CMD_NONE ||
	    cmd == SPECTRE_V2_CMD_RETPOLINE ||
	    cmd == SPECTRE_V2_CMD_RETPOLINE_GENERIC)
		nospec("");

	return cmd;
}

/* Check for Skylake-like CPUs (for RSB handling) */
/* Update the static key controlling the MDS CPU buffer clear in idle */
static void update_mds_branch_idle(void)
{
	u64 ia32_cap = x86_read_arch_cap_msr();

	/*
	 * Enable the idle clearing if SMT is active on CPUs which are
	 * affected only by MSBDS and not any other MDS variant.
	 *
	 * The other variants cannot be mitigated when SMT is enabled, so
	 * clearing the buffers on idle just to prevent the Store Buffer
	 * repartitioning leak would be a window dressing exercise.
	 */
	if (!x86_bug_msbds_only)
		return;

	if (sched_smt_active()) {
		mds_idle_clear = true;
	} else if (mmio_mitigation == MMIO_MITIGATION_OFF ||
		   (ia32_cap & ARCH_CAP_FBSDP_NO)) {
		mds_idle_clear = false;
	}
}

static void __init spectre_v2_select_mitigation(void)
{
	enum spectre_v2_mitigation_cmd cmd = spectre_v2_parse_cmdline();
	enum spectre_v2_mitigation mode = SPECTRE_V2_NONE;

	/*
	 * If the CPU is not affected and the command line mode is NONE or AUTO
	 * then nothing to do.
	 */
	if (!x86_bug_spectre_v2 &&
	    (cmd == SPECTRE_V2_CMD_NONE || cmd == SPECTRE_V2_CMD_AUTO))
		return;

	switch (cmd) {
	case SPECTRE_V2_CMD_NONE:
		return;

	case SPECTRE_V2_CMD_FORCE:
		/* FALLTRHU */
	case SPECTRE_V2_CMD_AUTO:

		if (boot_cpu_has_bug(X86_BUG_RETBLEED) &&
		    retbleed_cmd != RETBLEED_CMD_OFF &&
		    boot_cpu_has(X86_FEATURE_IBRS) &&
		    boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) {
			mode = SPECTRE_V2_IBRS;
			goto done;
		}

		goto retpoline_generic;

	case SPECTRE_V2_CMD_RETPOLINE_LFENCE:
#ifdef CONFIG_RETPOLINE
			goto retpoline_lfence;
#endif
		break;
	case SPECTRE_V2_CMD_RETPOLINE_GENERIC:
#ifdef CONFIG_RETPOLINE
			goto retpoline_generic;
#endif
		break;
	case SPECTRE_V2_CMD_RETPOLINE:
#ifdef CONFIG_RETPOLINE
			goto retpoline_generic;
#endif
		break;

	case SPECTRE_V2_CMD_IBRS:
		mode = SPECTRE_V2_IBRS;
		setup_force_cpu_cap(X86_FEATURE_SPEC_CTRL);
		break;
	}
	pr_err("kernel not compiled with retpoline; no mitigation available!");
	return;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {
	retpoline_lfence:
		if (!boot_cpu_has(X86_FEATURE_LFENCE_RDTSC)) {
			pr_err("LFENCE not serializing. Switching to generic retpoline\n");
			goto retpoline_generic;
		}
		mode = retp_compiler() ? SPECTRE_V2_LFENCE :
					 SPECTRE_V2_RETPOLINE_MINIMAL_AMD;
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE_LFENCE);
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
	} else {
	retpoline_generic:
		mode = retp_compiler() ? SPECTRE_V2_RETPOLINE :
					 SPECTRE_V2_RETPOLINE_MINIMAL;
		setup_force_cpu_cap(X86_FEATURE_RETPOLINE);
	}

	pr_info("Retpolines enabled, force-disabling IBRS\n");
	ibrs_state = 0;

done:
	spectre_v2_enabled = mode;
	pr_info("%s\n", spectre_v2_strings[mode]);

	/*
	 * If spectre v2 protection has been enabled, unconditionally fill
	 * RSB during a context switch; this protects against two independent
	 * issues:
	 *
	 *	- RSB underflow (and switch to BTB) on Skylake+
	 *	- SpectreRSB variant of spectre v2 on X86_BUG_SPECTRE_V2 CPUs
	 */
	setup_force_cpu_cap(X86_FEATURE_RSB_CTXSW);
	pr_info("Spectre v2 / SpectreRSB mitigation: Filling RSB on context switch\n");
	spectre_v2_cmd = cmd;
}

#undef pr_fmt
#define pr_fmt(fmt) fmt

#define MDS_MSG_SMT "MDS CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/mds.html for more details.\n"
#define TAA_MSG_SMT "TAA CPU bug present and SMT on, data leak possible. See https://www.kernel.org/doc/html/latest/admin-guide/hw-vuln/tsx_async_abort.html for more details.\n"

void arch_smt_update(void)
{
	mutex_lock(&spec_ctrl_mutex);
	switch (mds_mitigation) {
	case MDS_MITIGATION_FULL:
	case MDS_MITIGATION_VMWERV:
		if (sched_smt_active() && !x86_bug_msbds_only)
			pr_warn_once(MDS_MSG_SMT);
		update_mds_branch_idle();
		break;
	case MDS_MITIGATION_OFF:
		break;
	}

	switch (taa_mitigation) {
	case TAA_MITIGATION_VERW:
	case TAA_MITIGATION_UCODE_NEEDED:
		if (sched_smt_active())
			pr_warn_once(TAA_MSG_SMT);
		break;
	case TAA_MITIGATION_TSX_DISABLED:
	case TAA_MITIGATION_OFF:
		break;
	}

	mutex_unlock(&spec_ctrl_mutex);
}

#undef pr_fmt
#define pr_fmt(fmt) "L1TF: " fmt

/* Default mitigation for L1TF-affected CPUs */
enum l1tf_mitigations l1tf_mitigation = L1TF_MITIGATION_FLUSH;
EXPORT_SYMBOL_GPL(l1tf_mitigation);

enum vmx_l1d_flush_state l1tf_vmx_mitigation = VMENTER_L1D_FLUSH_AUTO;
EXPORT_SYMBOL_GPL(l1tf_vmx_mitigation);

/*
 * These CPUs all support 44bits physical address space internally in the
 * cache but CPUID can report a smaller number of physical address bits.
 *
 * The L1TF mitigation uses the top most address bit for the inversion of
 * non present PTEs. When the installed memory reaches into the top most
 * address bit due to memory holes, which has been observed on machines
 * which report 36bits physical address bits and have 32G RAM installed,
 * then the mitigation range check in l1tf_select_mitigation() triggers.
 * This is a false positive because the mitigation is still possible due to
 * the fact that the cache uses 44bit internally. Use the cache bits
 * instead of the reported physical bits and adjust them on the affected
 * machines to 44bit if the reported bits are less than 44.
 */
static void override_cache_bits(struct cpuinfo_x86 *c)
{
	c->x86_cache_bits = c->x86_phys_bits;

	if (c->x86 != 6)
		return;

	switch (c->x86_model) {
	case INTEL_FAM6_NEHALEM:
	case INTEL_FAM6_WESTMERE:
	case INTEL_FAM6_SANDYBRIDGE:
	case INTEL_FAM6_IVYBRIDGE:
	case INTEL_FAM6_HASWELL_CORE:
	case INTEL_FAM6_HASWELL_ULT:
	case INTEL_FAM6_HASWELL_GT3E:
	case INTEL_FAM6_BROADWELL_CORE:
	case INTEL_FAM6_BROADWELL_GT3E:
	case INTEL_FAM6_SKYLAKE_MOBILE:
	case INTEL_FAM6_SKYLAKE_DESKTOP:
	case INTEL_FAM6_KABYLAKE_MOBILE:
	case INTEL_FAM6_KABYLAKE_DESKTOP:
		if (c->x86_cache_bits < 44)
			c->x86_cache_bits = 44;
		break;
	}
}

static void __init l1tf_select_mitigation(void)
{
	u64 half_pa;

	if (!x86_bug_l1tf)
		return;

	if (cpu_mitigations_off())
		l1tf_mitigation = L1TF_MITIGATION_OFF;
	else if (cpu_mitigations_auto_nosmt())
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOSMT;

	override_cache_bits(&boot_cpu_data);

	switch (l1tf_mitigation) {
	case L1TF_MITIGATION_OFF:
	case L1TF_MITIGATION_FLUSH_NOWARN:
	case L1TF_MITIGATION_FLUSH:
		break;
	case L1TF_MITIGATION_FLUSH_NOSMT:
	case L1TF_MITIGATION_FULL:
		cpu_smt_disable(false);
		break;
	case L1TF_MITIGATION_FULL_FORCE:
		cpu_smt_disable(true);
		break;
	}

#if PAGETABLE_LEVELS == 2
	pr_warn("Kernel not compiled for PAE. No mitigation for L1TF\n");
	return;
#endif

	half_pa = (u64)l1tf_pfn_limit() << PAGE_SHIFT;
	if (e820_any_mapped(half_pa, ULLONG_MAX - half_pa, E820_RAM)) {
		pr_warn("System has more than MAX_PA/2 memory. L1TF mitigation not effective.\n");
		pr_info("You may make it effective by booting the kernel with mem=%llu parameter.\n",
				half_pa);
		pr_info("However, doing so will make a part of your RAM unusable.\n");
		pr_info("Reading https://www.kernel.org/doc/html/latest/admin-guide/l1tf.html might help you decide.\n");
		return;
	}

	setup_force_cpu_cap(X86_FEATURE_L1TF_FIX);
}

static int __init l1tf_cmdline(char *str)
{
	if (!arch_has_pfn_modify_check())
		return 0;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "off"))
		l1tf_mitigation = L1TF_MITIGATION_OFF;
	else if (!strcmp(str, "flush,nowarn"))
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOWARN;
	else if (!strcmp(str, "flush"))
		l1tf_mitigation = L1TF_MITIGATION_FLUSH;
	else if (!strcmp(str, "flush,nosmt"))
		l1tf_mitigation = L1TF_MITIGATION_FLUSH_NOSMT;
	else if (!strcmp(str, "full"))
		l1tf_mitigation = L1TF_MITIGATION_FULL;
	else if (!strcmp(str, "full,force"))
		l1tf_mitigation = L1TF_MITIGATION_FULL_FORCE;

	return 0;
}
early_param("l1tf", l1tf_cmdline);

#undef pr_fmt
#define pr_fmt(fmt)    "Speculative Store Bypass: " fmt

static enum ssb_mitigation ssb_mode = SPEC_STORE_BYPASS_NONE;

/* The kernel command line selection */
enum ssb_mitigation_cmd {
	SPEC_STORE_BYPASS_CMD_NONE,
	SPEC_STORE_BYPASS_CMD_AUTO,
	SPEC_STORE_BYPASS_CMD_ON,
	SPEC_STORE_BYPASS_CMD_PRCTL,
	SPEC_STORE_BYPASS_CMD_SECCOMP,
};

static enum ssb_mitigation_cmd ssb_cmd;

static const char *ssb_strings[] = {
	[SPEC_STORE_BYPASS_NONE]        = "Vulnerable",
	[SPEC_STORE_BYPASS_DISABLE]     = "Mitigation: Speculative Store Bypass disabled",
	[SPEC_STORE_BYPASS_PRCTL]	= "Mitigation: Speculative Store Bypass disabled via prctl",
	[SPEC_STORE_BYPASS_SECCOMP]	= "Mitigation: Speculative Store Bypass disabled via prctl and seccomp",
};

static const struct {
	const char *option;
	enum ssb_mitigation_cmd cmd;
} ssb_mitigation_options[] = {
	{ "auto",	SPEC_STORE_BYPASS_CMD_AUTO },    /* Platform decides */
	{ "on",		SPEC_STORE_BYPASS_CMD_ON },      /* Disable Speculative Store Bypass */
	{ "off",	SPEC_STORE_BYPASS_CMD_NONE },    /* Don't touch Speculative Store Bypass */
	{ "prctl",	SPEC_STORE_BYPASS_CMD_PRCTL },   /* Disable Speculative Store Bypass via prctl */
	{ "seccomp",	SPEC_STORE_BYPASS_CMD_SECCOMP }, /* Disable Speculative Store Bypass via prctl and seccomp */
};

static enum ssb_mitigation_cmd __init ssb_parse_cmdline(void)
{
	enum ssb_mitigation_cmd cmd = SPEC_STORE_BYPASS_CMD_AUTO;
	char arg[20];
	int ret, i;

	if (cmdline_find_option_bool(boot_command_line, "nospec_store_bypass_disable") ||
	    cpu_mitigations_off()) {
		return SPEC_STORE_BYPASS_CMD_NONE;
	} else {
		ret = cmdline_find_option(boot_command_line, "spec_store_bypass_disable",
				arg, sizeof(arg));
		if (ret < 0)
			return SPEC_STORE_BYPASS_CMD_AUTO;

		for (i = 0; i < ARRAY_SIZE(ssb_mitigation_options); i++) {
			if (!match_option(arg, ret, ssb_mitigation_options[i].option))
				continue;

			cmd = ssb_mitigation_options[i].cmd;
			break;
		}

		if (i >= ARRAY_SIZE(ssb_mitigation_options)) {
			pr_err("unknown option (%s). Switching to AUTO select\n", arg);
			return SPEC_STORE_BYPASS_CMD_AUTO;
		}
	}

	return cmd;
}

static void ssb_init_cmd_line(void)
{
	ssb_cmd = ssb_parse_cmdline();
}

static enum ssb_mitigation_cmd __ssb_select_mitigation(void)
{
	enum ssb_mitigation mode = SPEC_STORE_BYPASS_NONE;
	enum ssb_mitigation_cmd cmd;

	if (!boot_cpu_has(X86_FEATURE_SSBD))
		return mode;

	cmd = ssb_cmd;
	if (!x86_bug_spec_store_bypass &&
			(cmd == SPEC_STORE_BYPASS_CMD_NONE ||
			 cmd == SPEC_STORE_BYPASS_CMD_AUTO))
		return mode;

	switch (cmd) {
	case SPEC_STORE_BYPASS_CMD_AUTO:
	case SPEC_STORE_BYPASS_CMD_SECCOMP:
		/*
		 * Choose prctl+seccomp as the default mode if seccomp is
		 * enabled.
		 */
#ifdef CONFIG_SECCOMP
		mode = SPEC_STORE_BYPASS_SECCOMP;
#else
		mode = SPEC_STORE_BYPASS_PRCTL;
#endif
		break;
	case SPEC_STORE_BYPASS_CMD_ON:
		mode = SPEC_STORE_BYPASS_DISABLE;
		break;
	case SPEC_STORE_BYPASS_CMD_PRCTL:
		mode = SPEC_STORE_BYPASS_PRCTL;
		break;
	case SPEC_STORE_BYPASS_CMD_NONE:
		break;
	}

	/*
	 * We have three CPU feature flags that are in play here:
	 *  - X86_BUG_SPEC_STORE_BYPASS - CPU is susceptible.
	 *  - X86_FEATURE_SSBD - CPU is able to turn off speculative store bypass
	 *  - X86_FEATURE_SPEC_STORE_BYPASS_DISABLE - engage the mitigation
         */
	if (mode == SPEC_STORE_BYPASS_DISABLE) {
		setup_force_cpu_cap(X86_FEATURE_SPEC_STORE_BYPASS_DISABLE);
		/*
		 * Intel uses the SPEC CTRL MSR Bit(2) for this, while AMD uses
		 * a completely different MSR and bit dependent on family.
		 */
		switch (boot_cpu_data.x86_vendor) {
			case X86_VENDOR_INTEL:
				x86_spec_ctrl_base |= SPEC_CTRL_SSBD;
				x86_spec_ctrl_mask &= ~SPEC_CTRL_SSBD;
				write_spec_ctrl_current(x86_spec_ctrl_base, true);
				break;
			case X86_VENDOR_AMD:
				x86_amd_ssbd_disable();
				break;
		}
	}

	return mode;
}

void ssb_select_mitigation(void)
{
	ssb_mode = __ssb_select_mitigation();

	if (x86_bug_spec_store_bypass)
		pr_info("%s\n", ssb_strings[ssb_mode]);
}

bool itlb_multihit_kvm_mitigation;
EXPORT_SYMBOL_GPL(itlb_multihit_kvm_mitigation);

#undef pr_fmt
#define pr_fmt(fmt)     "Speculation prctl: " fmt

static int ssb_prctl_set(struct task_struct *task, unsigned long ctrl)
{
	bool update;

	if (ssb_mode != SPEC_STORE_BYPASS_PRCTL &&
	    ssb_mode != SPEC_STORE_BYPASS_SECCOMP)
		return -ENXIO;

	switch (ctrl) {
	case PR_SPEC_ENABLE:
		/* If speculation is force disabled, enable is not allowed */
		if (task_spec_ssb_force_disable(task))
			return -EPERM;
		task_clear_spec_ssb_disable(task);
		update = test_and_clear_tsk_thread_flag(task, TIF_SSBD);
		break;
	case PR_SPEC_DISABLE:
		task_set_spec_ssb_disable(task);
		update = !test_and_set_tsk_thread_flag(task, TIF_SSBD);
		break;
	case PR_SPEC_FORCE_DISABLE:
		task_set_spec_ssb_disable(task);
		task_set_spec_ssb_force_disable(task);
		update = !test_and_set_tsk_thread_flag(task, TIF_SSBD);
		break;
	default:
		return -ERANGE;
	}

	/*
	 * If being set on non-current task, delay setting the CPU
	 * mitigation until it is next scheduled.
	 */
	if (task == current && update)
		speculative_store_bypass_update();

	return 0;
}

static int ssb_prctl_get(struct task_struct *task)
{
	switch (ssb_mode) {
	case SPEC_STORE_BYPASS_DISABLE:
		return PR_SPEC_DISABLE;
	case SPEC_STORE_BYPASS_SECCOMP:
	case SPEC_STORE_BYPASS_PRCTL:
		if (task_spec_ssb_force_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE;
		if (task_spec_ssb_disable(task))
			return PR_SPEC_PRCTL | PR_SPEC_DISABLE;
		return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
	default:
		if (x86_bug_spec_store_bypass)
			return PR_SPEC_ENABLE;
		return PR_SPEC_NOT_AFFECTED;
	}
}

int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssb_prctl_set(task, ctrl);
	default:
		return -ENODEV;
	}
}

#ifdef CONFIG_SECCOMP
void arch_seccomp_spec_mitigate(struct task_struct *task)
{
	if (ssb_mode == SPEC_STORE_BYPASS_SECCOMP)
		ssb_prctl_set(task, PR_SPEC_FORCE_DISABLE);
}
#endif

int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which)
{
	switch (which) {
		case PR_SPEC_STORE_BYPASS:
			return ssb_prctl_get(task);
		default:
			return -ENODEV;
	}
}

void x86_spec_ctrl_setup_ap(void)
{
	if (boot_cpu_has(X86_FEATURE_IBRS))
		write_spec_ctrl_current(x86_spec_ctrl_base & ~x86_spec_ctrl_mask, true);

       if (ssb_mode == SPEC_STORE_BYPASS_DISABLE)
               x86_amd_ssbd_disable();
}

#ifdef CONFIG_SYSFS

#define L1TF_DEFAULT_MSG "Mitigation: PTE Inversion"

static const char *l1tf_vmx_states[] = {
	[VMENTER_L1D_FLUSH_AUTO]		= "auto",
	[VMENTER_L1D_FLUSH_NEVER]		= "vulnerable",
	[VMENTER_L1D_FLUSH_COND]		= "conditional cache flushes",
	[VMENTER_L1D_FLUSH_ALWAYS]		= "cache flushes",
	[VMENTER_L1D_FLUSH_EPT_DISABLED]	= "EPT disabled",
};

static ssize_t l1tf_show_state(char *buf)
{
	if (l1tf_vmx_mitigation == VMENTER_L1D_FLUSH_AUTO)
		return sprintf(buf, "%s\n", L1TF_DEFAULT_MSG);

	return sprintf(buf, "%s; VMX: SMT %s, L1D %s\n", L1TF_DEFAULT_MSG,
		       cpu_smt_control == CPU_SMT_ENABLED ? "vulnerable" : "disabled",
		       l1tf_vmx_states[l1tf_vmx_mitigation]);
}

ssize_t cpu_show_meltdown(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	if (!x86_bug_meltdown)
		return sprintf(buf, "Not affected\n");
	if (boot_cpu_has(X86_FEATURE_KAISER))
		return sprintf(buf, "Mitigation: PTI\n");
	return sprintf(buf, "Vulnerable\n");
}

ssize_t cpu_show_spectre_v1(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (!x86_bug_spectre_v1)
		return sprintf(buf, "Not affected\n");
	return sprintf(buf, "%s\n", spectre_v1_strings[spectre_v1_mitigation]);
}

ssize_t cpu_show_spectre_v2(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (!x86_bug_spectre_v2)
		return sprintf(buf, "Not affected\n");

	if (boot_cpu_has(X86_FEATURE_SPEC_CTRL) && x86_ibrs_enabled()) {
		return sprintf(buf, "Mitigation: IBRS+IBPB%s\n",
			spectre_v2_module_string());
	}

	if (x86_ibpb_enabled())
		return sprintf(buf, "%s + IBPB%s\n", spectre_v2_strings[spectre_v2_enabled],
			spectre_v2_module_string());
	else
		return sprintf(buf, "%s%s\n", spectre_v2_strings[spectre_v2_enabled],
			spectre_v2_module_string());
}

ssize_t __weak cpu_show_spec_store_bypass(struct device *dev,
                                          struct device_attribute *attr, char *buf)
{
	if (!x86_bug_spec_store_bypass)
		return sprintf(buf, "Not affected\n");
	return sprintf(buf, "%s\n", ssb_strings[ssb_mode]);
}
ssize_t cpu_show_l1tf(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	if (!x86_bug_l1tf)
		return sprintf(buf, "Not affected\n");
	return l1tf_show_state(buf);
}

ssize_t itlb_multihit_show_state(char *buf)
{
	return sprintf(buf, "Processor vulnerable\n");
}

static ssize_t mds_show_state(char *buf)
{
#ifndef CONFIG_XEN
	if (x86_hyper) {
		return sprintf(buf, "%s; SMT Host state unknown\n",
			       mds_strings[mds_mitigation]);
	}
#endif

	if (x86_bug_msbds_only) {
		return sprintf(buf, "%s; SMT %s\n", mds_strings[mds_mitigation],
			       (mds_mitigation == MDS_MITIGATION_OFF ? "vulnerable" :
			        sched_smt_active() ? "mitigated" : "disabled"));
	}

	return sprintf(buf, "%s; SMT %s\n", mds_strings[mds_mitigation],
		       sched_smt_active() ? "vulnerable" : "disabled");
}

ssize_t cpu_show_mds(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!x86_bug_mds)
		return sprintf(buf, "Not affected\n");

	return mds_show_state(buf);
}
#endif

static ssize_t tsx_async_abort_show_state(char *buf)
{
	if ((taa_mitigation == TAA_MITIGATION_TSX_DISABLED) ||
	    (taa_mitigation == TAA_MITIGATION_OFF))
		return sprintf(buf, "%s\n", taa_strings[taa_mitigation]);

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		return sprintf(buf, "%s; SMT Host state unknown\n",
			       taa_strings[taa_mitigation]);
	}

	return sprintf(buf, "%s; SMT %s\n", taa_strings[taa_mitigation],
		       sched_smt_active() ? "vulnerable" : "disabled");
}

ssize_t cpu_show_tsx_async_abort(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!x86_bug_taa)
		return sprintf(buf, "Not affected\n");

	return tsx_async_abort_show_state(buf);
}

ssize_t cpu_show_itlb_multihit(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!has_bug_itlb_multihit())
		return sprintf(buf, "Not affected\n");

	if (itlb_multihit_kvm_mitigation)
		return sprintf(buf, "KVM: Mitigation: Split huge pages\n");
	else
		return sprintf(buf, "KVM: Vulnerable\n");
}

ssize_t cpu_show_srbds(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!x86_bug_srbds)
		return sprintf(buf, "Not affected\n");

	return sprintf(buf, "%s\n", srbds_strings[srbds_mitigation]);
}

static ssize_t retbleed_show_state(char *buf)
{
	if (retbleed_mitigation == RETBLEED_MITIGATION_UNRET) {
	    if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		    return sprintf(buf, "Vulnerable: untrained return thunk on non-Zen uarch\n");

	    return sprintf(buf, "%s; SMT %s\n",
			   retbleed_strings[retbleed_mitigation],
			   !sched_smt_active() ? "disabled" : "vulnerable");
	}

	return sprintf(buf, "%s\n", retbleed_strings[retbleed_mitigation]);
}


ssize_t cpu_show_retbleed(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!x86_bug_retbleed)
		return sprintf(buf, "Not affected\n");

	return retbleed_show_state(buf);
}

ssize_t cpu_show_mmio_stale_data(struct device *dev, struct device_attribute *attr, char *buf)
{

	if (!x86_bug_mmio)
		return sprintf(buf, "Not affected\n");

	if (mmio_mitigation == MMIO_MITIGATION_OFF)
		return sprintf(buf, "%s\n", mmio_strings[mmio_mitigation]);

#ifndef CONFIG_XEN
	if (x86_hyper) {
		return sprintf(buf, "%s; SMT Host state unknown\n",
				  mmio_strings[mmio_mitigation]);
	}
#endif

	return sprintf(buf, "%s; SMT %s\n", mmio_strings[mmio_mitigation],
			  sched_smt_active() ? "vulnerable" : "disabled");
}


static ssize_t gds_show_state(char *buf)
{
	return sprintf(buf, "%s", gds_strings[gds_mitigation]);
}

ssize_t cpu_show_gds(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!x86_bug_gds)
		return sprintf(buf, "Not affected\n");

	return gds_show_state(buf);
}

void x86_spec_ctrl_set(u64 val)
{
	if (val & x86_spec_ctrl_mask)
		WARN_ONCE(1, "SPEC_CTRL MSR value 0x%16llx is unknown.\n", val);
	else
		write_spec_ctrl_current(x86_spec_ctrl_base | val, true);
}
EXPORT_SYMBOL_GPL(x86_spec_ctrl_set);

u64 x86_spec_ctrl_get_default(void)
{
	u64 msrval = x86_spec_ctrl_base;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		msrval |= ssbd_tif_to_spec_ctrl(current_thread_info()->flags);
	return msrval;
}
EXPORT_SYMBOL_GPL(x86_spec_ctrl_get_default);

void x86_spec_ctrl_set_guest(u64 guest_spec_ctrl)
{
	u64 host = x86_spec_ctrl_base;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		host |= ssbd_tif_to_spec_ctrl(current_thread_info()->flags);

	if (host != guest_spec_ctrl)
		wrmsrl(MSR_IA32_SPEC_CTRL, guest_spec_ctrl);
}
EXPORT_SYMBOL_GPL(x86_spec_ctrl_set_guest);

void x86_spec_ctrl_restore_host(u64 guest_spec_ctrl)
{
	u64 host = x86_spec_ctrl_base;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		host |= ssbd_tif_to_spec_ctrl(current_thread_info()->flags);

	if (host != guest_spec_ctrl)
		write_spec_ctrl_current(host, true);
}
EXPORT_SYMBOL_GPL(x86_spec_ctrl_restore_host);

static void x86_amd_ssbd_disable(void)
{
	u64 msrval = x86_amd_ls_cfg_base | x86_amd_ls_cfg_ssbd_mask;

#ifdef CONFIG_XEN
	if (x86_amd_ls_cfg_base & x86_amd_ls_cfg_ssbd_mask)
		return;
#endif

	if (boot_cpu_has(X86_FEATURE_AMD_SSBD))
		wrmsrl(MSR_AMD64_LS_CFG, msrval);
}


#ifdef CONFIG_SWAP
unsigned long max_swapfile_size(void)
{
	unsigned long pages;

	pages = generic_max_swapfile_size();

	if (x86_bug_l1tf) {
		unsigned long long l1tf_limit = l1tf_pfn_limit();
		/* Limit the swap file size to MAX_PA/2 for L1TF workaround */
		pages = min_t(unsigned long long, l1tf_limit, pages);
	}
	return pages;
}
#endif

void x86_sync_spec_ctrl(void)
{
}
EXPORT_SYMBOL_GPL(x86_sync_spec_ctrl);

/*
 * Simulate X86_BUG_<X> macros from upstream.
 */
bool boot_cpu_has_bug(enum x86_cpu_bugs bug)
{
	switch (bug) {
	case X86_BUG_TAA:	return x86_bug_taa;
	case X86_BUG_RETBLEED:	return x86_bug_retbleed;
	default:		return false;
	}
}
