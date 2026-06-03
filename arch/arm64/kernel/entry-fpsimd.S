/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FP/SIMD state saving and restoring
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/fpsimdmacros.h>

#ifdef CONFIG_ARM64_SVE

/*
 * Zero all SVE registers but the first 128-bits of each vector
 *
 * VQ must already be configured by caller, any further updates of VQ
 * will need to ensure that the register state remains valid.
 *
 * x0 = include FFR?
 * x1 = VQ - 1
 */
SYM_FUNC_START(sve_flush_live)
	cbz		x1, 1f	// A VQ-1 of 0 is 128 bits so no extra Z state
	sve_flush_z
1:	sve_flush_p
	tbz		x0, #0, 2f
	sve_flush_ffr
2:	ret
SYM_FUNC_END(sve_flush_live)

#endif /* CONFIG_ARM64_SVE */

#ifdef CONFIG_ARM64_SME

/*
 * Save the ZA and ZT state
 *
 * x0 - pointer to buffer for state
 * w1 - number of ZT registers to save
 */
SYM_FUNC_START(sme_save_state)
	_sme_rdsvl	2, 1		// x2 = VL/8
	sme_save_za 0, x2, 12		// Leaves x0 pointing to the end of ZA

	cbz	w1, 1f
	_str_zt 0
1:
	ret
SYM_FUNC_END(sme_save_state)

/*
 * Load the ZA and ZT state
 *
 * x0 - pointer to buffer for state
 * w1 - number of ZT registers to save
 */
SYM_FUNC_START(sme_load_state)
	_sme_rdsvl	2, 1		// x2 = VL/8
	sme_load_za 0, x2, 12		// Leaves x0 pointing to the end of ZA

	cbz	w1, 1f
	_ldr_zt 0
1:
	ret
SYM_FUNC_END(sme_load_state)

#endif /* CONFIG_ARM64_SME */
