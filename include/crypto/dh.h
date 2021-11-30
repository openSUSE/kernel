/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Diffie-Hellman secret to be used with kpp API along with helper functions
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */
#ifndef _CRYPTO_DH_
#define _CRYPTO_DH_

/**
 * DOC: DH Helper Functions
 *
 * To use DH with the KPP cipher API, the following data structure and
 * functions should be used.
 *
 * To use DH with KPP, the following functions should be used to operate on
 * a DH private key. The packet private key that can be set with
 * the KPP API function call of crypto_kpp_set_secret.
 */

/** enum dh_group_id - identify well-known domain parameter sets */
enum dh_group_id {
	DH_GROUP_ID_UNKNOWN = 0, /* Constants are used in test vectors. */
#ifdef CONFIG_CRYPTO_DH_GROUPS_RFC7919
	DH_GROUP_ID_FFDHE2048 = 1,
	DH_GROUP_ID_FFDHE3072 = 2,
	DH_GROUP_ID_FFDHE4096 = 3,
	DH_GROUP_ID_FFDHE6144 = 4,
	DH_GROUP_ID_FFDHE8192 = 5,
#endif
#ifdef CONFIG_CRYPTO_DH_GROUPS_RFC3526
	DH_GROUP_ID_MODP2048 = 6,
	DH_GROUP_ID_MODP3072 = 7,
	DH_GROUP_ID_MODP4096 = 8,
	DH_GROUP_ID_MODP6144 = 9,
	DH_GROUP_ID_MODP8192 = 10,
#endif
};

/**
 * struct dh - define a DH private key
 *
 * @key:	Private DH key
 * @p:		Diffie-Hellman parameter P
 * @g:		Diffie-Hellman generator G
 * @key_size:	Size of the private DH key
 * @p_size:	Size of DH parameter P
 * @g_size:	Size of DH generator G
 */
struct dh {
	enum dh_group_id group_id;
	const void *key;
	const void *p;
	const void *g;
	unsigned int key_size;
	unsigned int p_size;
	unsigned int g_size;
};

/**
 * crypto_dh_key_len() - Obtain the size of the private DH key
 * @params:	private DH key
 *
 * This function returns the packet DH key size. A caller can use that
 * with the provided DH private key reference to obtain the required
 * memory size to hold a packet key.
 *
 * Return: size of the key in bytes
 */
unsigned int crypto_dh_key_len(const struct dh *params);

/**
 * crypto_dh_encode_key() - encode the private key
 * @buf:	Buffer allocated by the caller to hold the packet DH
 *		private key. The buffer should be at least crypto_dh_key_len
 *		bytes in size.
 * @len:	Length of the packet private key buffer
 * @params:	Buffer with the caller-specified private key
 *
 * The DH implementations operate on a packet representation of the private
 * key.
 *
 * Return:	-EINVAL if buffer has insufficient size, 0 on success
 */
int crypto_dh_encode_key(char *buf, unsigned int len, const struct dh *params);

/**
 * crypto_dh_decode_key() - decode a private key
 * @buf:	Buffer holding a packet key that should be decoded
 * @len:	Length of the packet private key buffer
 * @params:	Buffer allocated by the caller that is filled with the
 *		unpacked DH private key.
 *
 * The unpacking obtains the private key by pointing @p to the correct location
 * in @buf. Thus, both pointers refer to the same memory.
 *
 * Return:	-EINVAL if buffer has insufficient size, 0 on success
 */
int crypto_dh_decode_key(const char *buf, unsigned int len, struct dh *params);

/*
 * The maximum key length is two times the max. sec. strength of the
 * safe-prime groups, rounded up to the next power of two.
 */
#define CRYPTO_DH_MAX_PRIVKEY_SIZE (512 / 8)

/**
 * crypto_dh_gen_privkey() - generate a DH private key
 * @buf:	The DH group to generate a key for
 * @key:	Buffer provided by the caller to receive the generated
 *		key
 * @key_size:	Pointer to an unsigned integer the generated key's length
 *		will be stored in
 *
 * This function is intended to generate an ephemeral DH key.
 *
 * Return:	Negative error code on failure, 0 on success
 */
int crypto_dh_gen_privkey(enum dh_group_id group_id,
			  char key[CRYPTO_DH_MAX_PRIVKEY_SIZE],
			  unsigned int *key_size);

#endif
