/*
 * Entropy functions used on early boot for KASLR base and memory
 * randomization. The base randomization is done in the compressed
 * kernel and memory randomization is done early when the regular
 * kernel starts. This file is included in the compressed kernel and
 * normally linked in the regular.
 */
#include <asm/asm.h>
#include <asm/kaslr.h>
#include <asm/msr.h>
#include <asm/archrandom.h>
#include <asm/e820/api.h>
#include <asm/io.h>

/*
 * When built for the regular kernel, several functions need to be stubbed out
 * or changed to their regular kernel equivalent.
 */
#ifndef KASLR_COMPRESSED_BOOT
#include <asm/cpufeature.h>
#include <asm/setup.h>

#define debug_putstr(v) early_printk("%s", v)
#define has_cpuflag(f) boot_cpu_has(f)
#define get_boot_seed() kaslr_offset()
#endif

#include "random.c"

unsigned long kaslr_get_random_long(const char *purpose)
{
	debug_putstr(" KASLR using");
	return get_random_long(purpose);
}
