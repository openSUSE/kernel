/* Module signature checker
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/verification.h>
#include <crypto/public_key.h>
#include <crypto/hash.h>
#include <keys/system_keyring.h>
#include "module-internal.h"

enum pkey_id_type {
	PKEY_ID_PGP,		/* OpenPGP generated key ID */
	PKEY_ID_X509,		/* X.509 arbitrary subjectKeyIdentifier */
	PKEY_ID_PKCS7,		/* Signature in PKCS#7 message */
};

/*
 * Module signature information block.
 *
 * The constituents of the signature section are, in order:
 *
 *	- Signer's name
 *	- Key identifier
 *	- Signature data
 *	- Information block
 */
struct module_signature {
	u8	algo;		/* Public-key crypto algorithm [0] */
	u8	hash;		/* Digest algorithm [0] */
	u8	id_type;	/* Key identifier type [PKEY_ID_PKCS7] */
	u8	signer_len;	/* Length of signer's name [0] */
	u8	key_id_len;	/* Length of key identifier [0] */
	u8	__pad[3];
	__be32	sig_len;	/* Length of signature data */
};

static int mod_is_hash_blacklisted(const void *mod, size_t verifylen)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	u8 *digest;
	int ret = 0;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		goto error_return;

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	digest_size = crypto_shash_digestsize(tfm);
	digest = kzalloc(digest_size + desc_size, GFP_KERNEL);
	if (!digest) {
		pr_err("digest memory buffer allocate fail\n");
		ret = -ENOMEM;
		goto error_digest;
	}
	desc = (void *)digest + digest_size;
	desc->tfm = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error_shash;

	ret = crypto_shash_finup(desc, mod, verifylen, digest);
	if (ret < 0)
		goto error_shash;

	pr_debug("%ld digest: %*phN\n", verifylen, (int) digest_size, digest);

	ret = is_hash_blacklisted(digest, digest_size, "bin");
	if (ret == -EKEYREJECTED)
		pr_err("Module hash %*phN is blacklisted\n",
		       (int) digest_size, digest);

error_shash:
	kfree(digest);
error_digest:
	crypto_free_shash(tfm);
error_return:
	return ret;
}

/*
 * Verify the signature on a module.
 */
int mod_verify_sig(const void *mod, unsigned long *_modlen)
{
	struct module_signature ms;
	size_t modlen = *_modlen, sig_len, wholelen;
	int ret;

	pr_devel("==>%s(,%zu)\n", __func__, modlen);

	if (modlen <= sizeof(ms))
		return -EBADMSG;

	wholelen = modlen + sizeof(MODULE_SIG_STRING) - 1;
	memcpy(&ms, mod + (modlen - sizeof(ms)), sizeof(ms));
	modlen -= sizeof(ms);

	sig_len = be32_to_cpu(ms.sig_len);
	if (sig_len >= modlen)
		return -EBADMSG;
	modlen -= sig_len;
	*_modlen = modlen;

	if (ms.id_type != PKEY_ID_PKCS7) {
		pr_err("Module is not signed with expected PKCS#7 message\n");
		return -ENOPKG;
	}

	if (ms.algo != 0 ||
	    ms.hash != 0 ||
	    ms.signer_len != 0 ||
	    ms.key_id_len != 0 ||
	    ms.__pad[0] != 0 ||
	    ms.__pad[1] != 0 ||
	    ms.__pad[2] != 0) {
		pr_err("PKCS#7 signature info has unexpected non-zero params\n");
		return -EBADMSG;
	}

	ret = verify_pkcs7_signature(mod, modlen, mod + modlen, sig_len,
				      (void *)1UL, VERIFYING_MODULE_SIGNATURE,
				      NULL, NULL);
	pr_devel("verify_pkcs7_signature() = %d\n", ret);

	/* checking hash of module is in blacklist */
	if (!ret)
		ret = mod_is_hash_blacklisted(mod, wholelen);

	return ret;
}
