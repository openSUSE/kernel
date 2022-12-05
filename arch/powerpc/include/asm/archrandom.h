/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_ARCHRANDOM_H
#define _ASM_POWERPC_ARCHRANDOM_H

#ifdef CONFIG_ARCH_RANDOM

bool __must_check arch_get_random_seed_long(unsigned long *v);

static inline bool __must_check arch_get_random_long(unsigned long *v)
{
	return false;
}

static inline bool __must_check arch_get_random_int(unsigned int *v)
{
	return false;
}


static inline bool __must_check arch_get_random_seed_int(unsigned int *v)
{
	unsigned long val;
	bool rc;

	rc = arch_get_random_seed_long(&val);
	if (rc)
		*v = val;

	return rc;
}
#endif /* CONFIG_ARCH_RANDOM */

#ifdef CONFIG_PPC_POWERNV
int pnv_get_random_long(unsigned long *v);
#endif

#endif /* _ASM_POWERPC_ARCHRANDOM_H */
