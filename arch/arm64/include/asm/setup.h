// SPDX-License-Identifier: GPL-2.0

#ifndef __ARM64_ASM_SETUP_H
#define __ARM64_ASM_SETUP_H

#include <linux/string.h>

#include <uapi/asm/setup.h>

void *get_early_fdt_ptr(void);
void early_fdt_map(u64 dt_phys);

static inline bool arch_parse_debug_rodata(char *arg)
{
	extern bool rodata_enabled;
	extern bool rodata_full;

	if (arg && !strcmp(arg, "full")) {
		rodata_enabled = true;
		rodata_full = true;
		return true;
	}

	return false;
}
#define arch_parse_debug_rodata arch_parse_debug_rodata

#endif
