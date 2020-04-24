#ifndef _CPU_DEVICE_ID
#define _CPU_DEVICE_ID 1

/*
 * Declare drivers belonging to specific x86 CPUs
 * Similar in spirit to pci_device_id and related PCI functions
 */
#include <linux/mod_devicetable.h>

/*
 * The wildcard initializers are in mod_devicetable.h because
 * file2alias needs them. Sigh.
 */

#define X86_FEATURE_MATCH(x) {			\
	.vendor		= X86_VENDOR_ANY,	\
	.family		= X86_FAMILY_ANY,	\
	.model		= X86_MODEL_ANY,	\
	.feature	= x,			\
}

extern const struct x86_cpu_id *x86_match_cpu(const struct x86_cpu_id *match);

#endif
