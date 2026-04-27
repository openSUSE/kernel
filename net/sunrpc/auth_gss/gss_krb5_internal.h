/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * SunRPC GSS Kerberos 5 mechanism internal definitions
 *
 * Copyright (c) 2022 Oracle and/or its affiliates.
 */

#ifndef _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H
#define _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H

#include <crypto/krb5.h>

/*
 * The RFCs often specify payload lengths in bits. This helper
 * converts a specified bit-length to the number of octets/bytes.
 */
#define BITS2OCTETS(x)	((x) / 8)

struct krb5_ctx;

struct gss_krb5_enctype {
	const u32		etype;		/* encryption (key) type */
	const u32		ctype;		/* checksum type */
	const char		*name;		/* "friendly" name */
	const char		*encrypt_name;	/* crypto encrypt name */
	const char		*aux_cipher;	/* aux encrypt cipher name */
	const char		*cksum_name;	/* crypto checksum name */
	const u16		signalg;	/* signing algorithm */
	const u16		sealalg;	/* sealing algorithm */
	const u32		cksumlength;	/* checksum length */
	const u32		keyed_cksum;	/* is it a keyed cksum? */
	const u32		keybytes;	/* raw key len, in bytes */
	const u32		keylength;	/* protocol key length, in octets */
	const u32		Kc_length;	/* checksum subkey length, in octets */
	const u32		Ke_length;	/* encryption subkey length, in octets */
	const u32		Ki_length;	/* integrity subkey length, in octets */

	int (*derive_key)(const struct gss_krb5_enctype *gk5e,
			  const struct xdr_netobj *in,
			  struct xdr_netobj *out,
			  const struct xdr_netobj *label,
			  gfp_t gfp_mask);
};

/* krb5_ctx flags definitions */
#define KRB5_CTX_FLAG_INITIATOR         0x00000001
#define KRB5_CTX_FLAG_ACCEPTOR_SUBKEY   0x00000004

struct krb5_ctx {
	int			initiate; /* 1 = initiating, 0 = accepting */
	u32			enctype;
	u32			flags;
	const struct gss_krb5_enctype *gk5e; /* enctype-specific info */
	const struct krb5_enctype *krb5e; /* crypto/krb5 enctype */
	struct crypto_aead	*initiator_enc_aead;
	struct crypto_aead	*acceptor_enc_aead;
	struct crypto_shash	*initiator_sign_shash;
	struct crypto_shash	*acceptor_sign_shash;
	u8			Ksess[GSS_KRB5_MAX_KEYLEN]; /* session key */
	u8			cksum[GSS_KRB5_MAX_KEYLEN];
	atomic_t		seq_send;
	atomic64_t		seq_send64;
	time64_t		endtime;
	struct xdr_netobj	mech_used;
};

/*
 * GSS Kerberos 5 mechanism Per-Message calls.
 */

u32 gss_krb5_get_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *text,
			struct xdr_netobj *token);

u32 gss_krb5_verify_mic_v2(struct krb5_ctx *ctx, struct xdr_buf *message_buffer,
			   struct xdr_netobj *read_token);

u32 gss_krb5_wrap_v2(struct krb5_ctx *kctx, int offset,
		     struct xdr_buf *buf, struct page **pages);

u32 gss_krb5_unwrap_v2(struct krb5_ctx *kctx, int offset, int len,
		       struct xdr_buf *buf, unsigned int *slack,
		       unsigned int *align);

/*
 * Implementation internal functions
 */

/* Key Derivation Functions */

int krb5_derive_key_v2(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *label,
		       gfp_t gfp_mask);

int krb5_kdf_hmac_sha2(const struct gss_krb5_enctype *gk5e,
		       const struct xdr_netobj *inkey,
		       struct xdr_netobj *outkey,
		       const struct xdr_netobj *in_constant,
		       gfp_t gfp_mask);

int krb5_kdf_feedback_cmac(const struct gss_krb5_enctype *gk5e,
			   const struct xdr_netobj *inkey,
			   struct xdr_netobj *outkey,
			   const struct xdr_netobj *in_constant,
			   gfp_t gfp_mask);

void krb5_make_confounder(u8 *p, int conflen);

u32 gss_krb5_checksum(struct crypto_ahash *tfm, char *header, int hdrlen,
		      const struct xdr_buf *body, int body_offset,
		      struct xdr_netobj *cksumout);

u32 krb5_encrypt(struct crypto_sync_skcipher *key, void *iv, void *in,
		 void *out, int length);

int xdr_extend_head(struct xdr_buf *buf, unsigned int base,
		    unsigned int shiftlen);

u32 gss_krb5_errno_to_status(int err);

int gss_krb5_mic_build_sg(const struct xdr_buf *body,
			  void *cksum, unsigned int cksum_len,
			  void *hdr,
			  struct scatterlist *sg_head,
			  struct scatterlist **sg_overflow);

u32 gss_krb5_aead_encrypt(struct krb5_ctx *kctx, u32 offset,
			  struct xdr_buf *buf, struct page **pages);
u32 gss_krb5_aead_decrypt(struct krb5_ctx *kctx, u32 offset, u32 len,
			  struct xdr_buf *buf, u32 *headskip, u32 *tailskip);

#if IS_ENABLED(CONFIG_KUNIT)
void krb5_nfold(u32 inbits, const u8 *in, u32 outbits, u8 *out);
const struct gss_krb5_enctype *gss_krb5_lookup_enctype(u32 etype);
int krb5_cbc_cts_encrypt(struct crypto_sync_skcipher *cts_tfm,
			 struct crypto_sync_skcipher *cbc_tfm, u32 offset,
			 struct xdr_buf *buf, struct page **pages,
			 u8 *iv, unsigned int ivsize);
int krb5_cbc_cts_decrypt(struct crypto_sync_skcipher *cts_tfm,
			 struct crypto_sync_skcipher *cbc_tfm,
			 u32 offset, struct xdr_buf *buf);
u32 krb5_etm_checksum(struct crypto_sync_skcipher *cipher,
		      struct crypto_ahash *tfm, const struct xdr_buf *body,
		      int body_offset, struct xdr_netobj *cksumout);
#endif

#endif /* _NET_SUNRPC_AUTH_GSS_KRB5_INTERNAL_H */
