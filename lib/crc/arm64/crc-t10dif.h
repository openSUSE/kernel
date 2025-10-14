// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated CRC-T10DIF using arm64 NEON and Crypto Extensions instructions
 *
 * Copyright (C) 2016 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/cpufeature.h>

#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_asimd);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pmull);

#define CRC_T10DIF_PMULL_CHUNK_SIZE	16U

asmlinkage void crc_t10dif_pmull_p8(u16 init_crc, const u8 *buf, size_t len,
				    u8 out[16]);
asmlinkage u16 crc_t10dif_pmull_p64(u16 init_crc, const u8 *buf, size_t len);

static inline u16 crc_t10dif_arch(u16 crc, const u8 *data, size_t length)
{
	if (length >= CRC_T10DIF_PMULL_CHUNK_SIZE) {
		if (static_branch_likely(&have_pmull)) {
			if (likely(may_use_simd())) {
				kernel_neon_begin();
				crc = crc_t10dif_pmull_p64(crc, data, length);
				kernel_neon_end();
				return crc;
			}
		} else if (length > CRC_T10DIF_PMULL_CHUNK_SIZE &&
			   static_branch_likely(&have_asimd) &&
			   likely(may_use_simd())) {
			u8 buf[16];

			kernel_neon_begin();
			crc_t10dif_pmull_p8(crc, data, length, buf);
			kernel_neon_end();

			return crc_t10dif_generic(0, buf, sizeof(buf));
		}
	}
	return crc_t10dif_generic(crc, data, length);
}

#define crc_t10dif_mod_init_arch crc_t10dif_mod_init_arch
static void crc_t10dif_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_asimd);
		if (cpu_have_named_feature(PMULL))
			static_branch_enable(&have_pmull);
	}
}
