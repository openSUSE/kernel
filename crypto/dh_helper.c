// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/string.h>
#include <crypto/dh.h>
#include <crypto/kpp.h>

#define DH_KPP_SECRET_MIN_SIZE (sizeof(struct kpp_secret) + 4 * sizeof(int))

static const struct safe_prime_group
{
	enum dh_group_id group_id;
	unsigned int max_strength;
	unsigned int p_size;
	const char *p;
} safe_prime_groups[] = {};

/* 2 is used as a generator for all safe-prime groups. */
static const char safe_prime_group_g[]  = { 2 };

static inline const struct safe_prime_group *
get_safe_prime_group(enum dh_group_id group_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(safe_prime_groups); ++i) {
		if (safe_prime_groups[i].group_id == group_id)
			return &safe_prime_groups[i];
	}

	return NULL;
}

static inline u8 *dh_pack_data(u8 *dst, u8 *end, const void *src, size_t size)
{
	if (!dst || size > end - dst)
		return NULL;
	memcpy(dst, src, size);
	return dst + size;
}

static inline const u8 *dh_unpack_data(void *dst, const void *src, size_t size)
{
	memcpy(dst, src, size);
	return src + size;
}

static inline unsigned int dh_data_size(const struct dh *p)
{
	if (p->group_id == DH_GROUP_ID_UNKNOWN)
		return p->key_size + p->p_size + p->g_size;
	else
		return p->key_size;
}

unsigned int crypto_dh_key_len(const struct dh *p)
{
	return DH_KPP_SECRET_MIN_SIZE + dh_data_size(p);
}
EXPORT_SYMBOL_GPL(crypto_dh_key_len);

int crypto_dh_encode_key(char *buf, unsigned int len, const struct dh *params)
{
	u8 *ptr = buf;
	u8 * const end = ptr + len;
	struct kpp_secret secret = {
		.type = CRYPTO_KPP_SECRET_TYPE_DH,
		.len = len
	};
	int group_id;

	if (unlikely(!len))
		return -EINVAL;

	ptr = dh_pack_data(ptr, end, &secret, sizeof(secret));
	group_id = (int)params->group_id;
	ptr = dh_pack_data(ptr, end, &group_id, sizeof(group_id));
	ptr = dh_pack_data(ptr, end, &params->key_size,
			   sizeof(params->key_size));
	ptr = dh_pack_data(ptr, end, &params->p_size, sizeof(params->p_size));
	ptr = dh_pack_data(ptr, end, &params->g_size, sizeof(params->g_size));
	ptr = dh_pack_data(ptr, end, params->key, params->key_size);
	if (params->group_id == DH_GROUP_ID_UNKNOWN) {
		ptr = dh_pack_data(ptr, end, params->p, params->p_size);
		ptr = dh_pack_data(ptr, end, params->g, params->g_size);
	}

	if (ptr != end)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_dh_encode_key);

int crypto_dh_decode_key(const char *buf, unsigned int len, struct dh *params)
{
	const u8 *ptr = buf;
	struct kpp_secret secret;
	int group_id;

	if (unlikely(!buf || len < DH_KPP_SECRET_MIN_SIZE))
		return -EINVAL;

	ptr = dh_unpack_data(&secret, ptr, sizeof(secret));
	if (secret.type != CRYPTO_KPP_SECRET_TYPE_DH)
		return -EINVAL;

	ptr = dh_unpack_data(&group_id, ptr, sizeof(group_id));
	params->group_id = (enum dh_group_id)group_id;
	ptr = dh_unpack_data(&params->key_size, ptr, sizeof(params->key_size));
	ptr = dh_unpack_data(&params->p_size, ptr, sizeof(params->p_size));
	ptr = dh_unpack_data(&params->g_size, ptr, sizeof(params->g_size));
	if (secret.len != crypto_dh_key_len(params))
		return -EINVAL;

	if (params->group_id == DH_GROUP_ID_UNKNOWN) {
		/* Don't allocate memory. Set pointers to data within
		 * the given buffer
		 */
		params->key = (void *)ptr;
		params->p = (void *)(ptr + params->key_size);
		params->g = (void *)(ptr + params->key_size + params->p_size);

		/*
		 * Don't permit 'p' to be 0.  It's not a prime number,
		 * and it's subject to corner cases such as 'mod 0'
		 * being undefined or crypto_kpp_maxsize() returning
		 * 0.
		 */
		if (memchr_inv(params->p, 0, params->p_size) == NULL)
			return -EINVAL;

	} else {
		const struct safe_prime_group *g;

		g = get_safe_prime_group(params->group_id);
		if (!g)
			return -EINVAL;

		params->key = (void *)ptr;

		params->p = g->p;
		params->p_size = g->p_size;
		params->g = safe_prime_group_g;
		params->g_size = sizeof(safe_prime_group_g);
	}

	/*
	 * Don't permit the buffer for 'key' or 'g' to be larger than 'p', since
	 * some drivers assume otherwise.
	 */
	if (params->key_size > params->p_size ||
	    params->g_size > params->p_size)
		return -EINVAL;


	return 0;
}
EXPORT_SYMBOL_GPL(crypto_dh_decode_key);
