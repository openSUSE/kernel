#include <asm-generic/export.h>
#include <asm/segment.h>
#include <asm/cache.h>


/*
 * Define the VERW operand that is disguised as entry code so that
 * it can be referenced with KPTI enabled. This ensure VERW can be
 * used late in exit-to-user path after page tables are switched.
 */
.pushsection .entry.text, "ax"

.align L1_CACHE_BYTES, 0xcc
	.globl mds_verw_sel
mds_verw_sel:
	.word __KERNEL_DS
.align L1_CACHE_BYTES, 0xcc
	.type mds_verw_sel STT_NOTYPE
	.set .L__sym_size_mds_verw_sel, .-mds_verw_sel
	.size mds_verw_sel, .L__sym_size_mds_verw_sel

/* For KVM */
EXPORT_SYMBOL_GPL(mds_verw_sel);

.popsection

