// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit tests for RxRPC crypto functions
 *
 * Copyright 2026 Google LLC
 */
#include "../ar-internal.h"
#include <kunit/test.h>

struct fcrypt_pcbc_testvec {
	u8 key[FCRYPT_BSIZE];
	u8 iv[FCRYPT_BSIZE];
	const u8 *ptext; /* plaintext */
	const u8 *ctext; /* ciphertext */
	size_t nblocks; /* length of ptext and ctext in blocks */
};

/* FCrypt-PCBC test vectors */
static const struct fcrypt_pcbc_testvec fcrypt_pcbc_testvecs[] = {
	{
		/* http://www.openafs.org/pipermail/openafs-devel/2000-December/005320.html */
		.key = "\x00\x00\x00\x00\x00\x00\x00\x00",
		.iv = "\x00\x00\x00\x00\x00\x00\x00\x00",
		.ptext = "\x00\x00\x00\x00\x00\x00\x00\x00",
		.ctext = "\x0E\x09\x00\xC7\x3E\xF7\xED\x41",
		.nblocks = 1,
	},
	{
		.key = "\x11\x44\x77\xAA\xDD\x00\x33\x66",
		.iv = "\x00\x00\x00\x00\x00\x00\x00\x00",
		.ptext = "\x12\x34\x56\x78\x9A\xBC\xDE\xF0",
		.ctext = "\xD8\xED\x78\x74\x77\xEC\x06\x80",
		.nblocks = 1,
	},
	{
		/* From Arla */
		.key = "\xf0\xe1\xd2\xc3\xb4\xa5\x96\x87",
		.iv = "\xfe\xdc\xba\x98\x76\x54\x32\x10",
		.ptext = "The quick brown fox jumps over the lazy dogs.\0\0",
		.ctext = "\x00\xf0\x0e\x11\x75\xe6\x23\x82"
			 "\xee\xac\x98\x62\x44\x51\xe4\x84"
			 "\xc3\x59\xd8\xaa\x64\x60\xae\xf7"
			 "\xd2\xd9\x13\x79\x72\xa3\x45\x03"
			 "\x23\xb5\x62\xd7\x0c\xf5\x27\xd1"
			 "\xf8\x91\x3c\xac\x44\x22\x92\xef",
		.nblocks = 6,
	},
	{
		.key = "\xfe\xdc\xba\x98\x76\x54\x32\x10",
		.iv = "\xf0\xe1\xd2\xc3\xb4\xa5\x96\x87",
		.ptext = "The quick brown fox jumps over the lazy dogs.\0\0",
		.ctext = "\xca\x90\xf5\x9d\xcb\xd4\xd2\x3c"
			 "\x01\x88\x7f\x3e\x31\x6e\x62\x9d"
			 "\xd8\xe0\x57\xa3\x06\x3a\x42\x58"
			 "\x2a\x28\xfe\x72\x52\x2f\xdd\xe0"
			 "\x19\x89\x09\x1c\x2a\x8e\x8c\x94"
			 "\xfc\xc7\x68\xe4\x88\xaa\xde\x0f",
		.nblocks = 6,
	}
};

static void test_fcrypt_pcbc(struct kunit *test)
{
	u8 data[48];

	for (size_t i = 0; i < ARRAY_SIZE(fcrypt_pcbc_testvecs); i++) {
		const struct fcrypt_pcbc_testvec *tv = &fcrypt_pcbc_testvecs[i];
		const size_t nblocks = tv->nblocks;
		const size_t len = nblocks * FCRYPT_BSIZE;
		struct fcrypt_key key;

		KUNIT_ASSERT_GE(test, sizeof(data), len);

		fcrypt_preparekey(&key, tv->key);

		/* out-of-place encryption */
		fcrypt_pcbc_encrypt(&key, tv->iv, tv->ptext, data, nblocks);
		KUNIT_ASSERT_MEMEQ(test, tv->ctext, data, len);

		/* in-place encryption */
		memcpy(data, tv->ptext, len);
		fcrypt_pcbc_encrypt(&key, tv->iv, data, data, nblocks);
		KUNIT_ASSERT_MEMEQ(test, tv->ctext, data, len);

		/* out-of-place decryption */
		fcrypt_pcbc_decrypt(&key, tv->iv, tv->ctext, data, nblocks);
		KUNIT_ASSERT_MEMEQ(test, tv->ptext, data, len);

		/* in-place decryption */
		memcpy(data, tv->ctext, len);
		fcrypt_pcbc_decrypt(&key, tv->iv, data, data, nblocks);
		KUNIT_ASSERT_MEMEQ(test, tv->ptext, data, len);
	}
}

static struct kunit_case rxrpc_test_cases[] = {
	KUNIT_CASE(test_fcrypt_pcbc),
	{},
};

static struct kunit_suite rxrpc_test_suite = {
	.name = "rxrpc",
	.test_cases = rxrpc_test_cases,
};
kunit_test_suite(rxrpc_test_suite);

MODULE_DESCRIPTION("Unit tests for RxRPC crypto functions");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
MODULE_LICENSE("GPL");
