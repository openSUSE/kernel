// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Hannes Reinecke, SUSE Linux
 */

#include <linux/crc32.h>
#include <linux/base64.h>
#include <linux/prandom.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/dh.h>
#include "nvme.h"
#include "fabrics.h"
#include "auth.h"

static u32 nvme_dhchap_seqnum;
static DEFINE_MUTEX(nvme_dhchap_mutex);

struct nvme_dhchap_queue_context {
	struct list_head entry;
	struct work_struct auth_work;
	struct nvme_ctrl *ctrl;
	struct crypto_shash *shash_tfm;
	struct crypto_kpp *dh_tfm;
	void *buf;
	size_t buf_size;
	int qid;
	int error;
	u32 s1;
	u32 s2;
	u16 transaction;
	u8 status;
	u8 hash_id;
	size_t hash_len;
	u8 dhgroup_id;
	u8 c1[64];
	u8 c2[64];
	u8 response[64];
	u8 *host_response;
	u8 *ctrl_key;
	int ctrl_key_len;
	u8 *host_key;
	int host_key_len;
	u8 *sess_key;
	int sess_key_len;
};

u32 nvme_auth_get_seqnum(void)
{
	u32 seqnum;

	mutex_lock(&nvme_dhchap_mutex);
	if (!nvme_dhchap_seqnum)
		nvme_dhchap_seqnum = prandom_u32();
	else {
		nvme_dhchap_seqnum++;
		if (!nvme_dhchap_seqnum)
			nvme_dhchap_seqnum++;
	}
	seqnum = nvme_dhchap_seqnum;
	mutex_unlock(&nvme_dhchap_mutex);
	return seqnum;
}
EXPORT_SYMBOL_GPL(nvme_auth_get_seqnum);

static struct nvme_auth_dhgroup_map {
	u8 id;
	const char name[16];
	const char kpp[16];
	enum dh_group_id group_id;
} dhgroup_map[] = {
	{ .id = NVME_AUTH_DHGROUP_NULL,
	  .name = "null", .kpp = "null",
	  .group_id = DH_GROUP_ID_UNKNOWN },
	{ .id = NVME_AUTH_DHGROUP_2048,
	  .name = "ffdhe2048", .kpp = "dh",
	  .group_id = DH_GROUP_ID_FFDHE2048 },
	{ .id = NVME_AUTH_DHGROUP_3072,
	  .name = "ffdhe3072", .kpp = "dh",
	  .group_id = DH_GROUP_ID_FFDHE3072 },
	{ .id = NVME_AUTH_DHGROUP_4096,
	  .name = "ffdhe4096", .kpp = "dh",
	  .group_id = DH_GROUP_ID_FFDHE4096 },
	{ .id = NVME_AUTH_DHGROUP_6144,
	  .name = "ffdhe6144", .kpp = "dh",
	  .group_id = DH_GROUP_ID_FFDHE6144 },
	{ .id = NVME_AUTH_DHGROUP_8192,
	  .name = "ffdhe8192", .kpp = "dh",
	  .group_id = DH_GROUP_ID_FFDHE8192 },
};

const char *nvme_auth_dhgroup_name(u8 dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].name;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_name);

enum dh_group_id nvme_auth_dhgroup_group_id(u8 dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].group_id;
	}
	return DH_GROUP_ID_UNKNOWN;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_group_id);

const char *nvme_auth_dhgroup_kpp(u8 dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].kpp;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_kpp);

u8 nvme_auth_dhgroup_id(const char *dhgroup_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (!strncmp(dhgroup_map[i].name, dhgroup_name,
			     strlen(dhgroup_map[i].name)))
			return dhgroup_map[i].id;
	}
	return NVME_AUTH_DHGROUP_INVALID;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_id);

static struct nvme_dhchap_hash_map {
	int id;
	int len;
	const char hmac[15];
	const char digest[15];
} hash_map[] = {
	{.id = NVME_AUTH_HASH_SHA256, .len = 32,
	 .hmac = "hmac(sha256)", .digest = "sha256" },
	{.id = NVME_AUTH_HASH_SHA384, .len = 48,
	 .hmac = "hmac(sha384)", .digest = "sha384" },
	{.id = NVME_AUTH_HASH_SHA512, .len = 64,
	 .hmac = "hmac(sha512)", .digest = "sha512" },
};

const char *nvme_auth_hmac_name(u8 hmac_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (hash_map[i].id == hmac_id)
			return hash_map[i].hmac;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_name);

const char *nvme_auth_digest_name(u8 hmac_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (hash_map[i].id == hmac_id)
			return hash_map[i].digest;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_digest_name);

u8 nvme_auth_hmac_id(const char *hmac_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (!strncmp(hash_map[i].hmac, hmac_name,
			     strlen(hash_map[i].hmac)))
			return hash_map[i].id;
	}
	return NVME_AUTH_HASH_INVALID;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_id);

size_t nvme_auth_hmac_hash_len(u8 hmac_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (hash_map[i].id == hmac_id)
			return hash_map[i].len;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_hash_len);

struct nvme_dhchap_key *nvme_auth_extract_key(unsigned char *secret,
					      u8 key_hash)
{
	struct nvme_dhchap_key *key;
	unsigned char *p;
	u32 crc;
	int ret, key_len;
	size_t allocated_len = strlen(secret);

	/* Secret might be affixed with a ':' */
	p = strrchr(secret, ':');
	if (p)
		allocated_len = p - secret;
	key = kzalloc(sizeof(*key), GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);
	key->key = kzalloc(allocated_len, GFP_KERNEL);
	if (!key->key) {
		ret = -ENOMEM;
		goto out_free_key;
	}

	key_len = base64_decode(secret, allocated_len, key->key);
	if (key_len < 0) {
		pr_debug("base64 key decoding error %d\n",
			 key_len);
		ret = key_len;
		goto out_free_secret;
	}

	if (key_len != 36 && key_len != 52 &&
	    key_len != 68) {
		pr_err("Invalid DH-HMAC-CHAP key len %d\n",
		       key_len);
		ret = -EINVAL;
		goto out_free_secret;
	}

	if (key_hash > 0 &&
	    (key_len - 4) != nvme_auth_hmac_hash_len(key_hash)) {
		pr_err("Invalid DH-HMAC-CHAP key len %d for %s\n", key_len,
		       nvme_auth_hmac_name(key_hash));
		ret = -EINVAL;
		goto out_free_secret;
	}

	/* The last four bytes is the CRC in little-endian format */
	key_len -= 4;
	/*
	 * The linux implementation doesn't do pre- and post-increments,
	 * so we have to do it manually.
	 */
	crc = ~crc32(~0, key->key, key_len);

	if (get_unaligned_le32(key->key + key_len) != crc) {
		pr_err("DH-HMAC-CHAP key crc mismatch (key %08x, crc %08x)\n",
		       get_unaligned_le32(key->key + key_len), crc);
		ret = -EKEYREJECTED;
		goto out_free_secret;
	}
	key->len = key_len;
	key->hash = key_hash;
	return key;
out_free_secret:
	kfree_sensitive(key->key);
out_free_key:
	kfree(key);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(nvme_auth_extract_key);

void nvme_auth_free_key(struct nvme_dhchap_key *key)
{
	if (!key)
		return;
	kfree_sensitive(key->key);
	kfree(key);
}
EXPORT_SYMBOL_GPL(nvme_auth_free_key);

u8 *nvme_auth_transform_key(struct nvme_dhchap_key *key, char *nqn)
{
	const char *hmac_name = nvme_auth_hmac_name(key->hash);
	struct crypto_shash *key_tfm;
	struct shash_desc *shash;
	u8 *transformed_key;
	int ret;

	if (key->hash == 0) {
		transformed_key = kmemdup(key->key, key->len, GFP_KERNEL);
		return transformed_key ? transformed_key : ERR_PTR(-ENOMEM);
	}

	if (!key || !key->key) {
		pr_warn("No key specified\n");
		return ERR_PTR(-ENOKEY);
	}
	if (!hmac_name) {
		pr_warn("Invalid key hash id %d\n", key->hash);
		return ERR_PTR(-EINVAL);
	}

	key_tfm = crypto_alloc_shash(hmac_name, 0, 0);
	if (IS_ERR(key_tfm))
		return (u8 *)key_tfm;

	shash = kmalloc(sizeof(struct shash_desc) +
			crypto_shash_descsize(key_tfm),
			GFP_KERNEL);
	if (!shash) {
		ret = -ENOMEM;
		goto out_free_key;
	}

	transformed_key = kzalloc(crypto_shash_digestsize(key_tfm), GFP_KERNEL);
	if (!transformed_key) {
		ret = -ENOMEM;
		goto out_free_shash;
	}

	shash->tfm = key_tfm;
	ret = crypto_shash_setkey(key_tfm, key->key, key->len);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_init(shash);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_update(shash, nqn, strlen(nqn));
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_update(shash, "NVMe-over-Fabrics", 17);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_final(shash, transformed_key);
out_free_shash:
	kfree(shash);
out_free_key:
	crypto_free_shash(key_tfm);
	if (ret < 0) {
		kfree_sensitive(transformed_key);
		return ERR_PTR(ret);
	}
	return transformed_key;
}
EXPORT_SYMBOL_GPL(nvme_auth_transform_key);

static int nvme_auth_hash_skey(int hmac_id, u8 *skey, size_t skey_len, u8 *hkey)
{
	const char *digest_name;
	struct crypto_shash *tfm;
	int ret;

	digest_name = nvme_auth_digest_name(hmac_id);
	if (!digest_name) {
		pr_debug("%s: failed to get digest for %d\n", __func__,
			 hmac_id);
		return -EINVAL;
	}
	tfm = crypto_alloc_shash(digest_name, 0, 0);
	if (IS_ERR(tfm))
		return -ENOMEM;

	ret = crypto_shash_tfm_digest(tfm, skey, skey_len, hkey);
	if (ret < 0)
		pr_debug("%s: Failed to hash digest len %zu\n", __func__,
			 skey_len);

	crypto_free_shash(tfm);
	return ret;
}

int nvme_auth_augmented_challenge(u8 hmac_id, u8 *skey, size_t skey_len,
		u8 *challenge, u8 *aug, size_t hlen)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 *hashed_key;
	const char *hmac_name;
	int ret;

	hashed_key = kmalloc(hlen, GFP_KERNEL);
	if (!hashed_key)
		return -ENOMEM;

	ret = nvme_auth_hash_skey(hmac_id, skey,
				  skey_len, hashed_key);
	if (ret < 0)
		goto out_free_key;

	hmac_name = nvme_auth_hmac_name(hmac_id);
	if (!hmac_name) {
		pr_warn("%s: invalid hash algoritm %d\n",
			__func__, hmac_id);
		ret = -EINVAL;
		goto out_free_key;
	}

	tfm = crypto_alloc_shash(hmac_name, 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		goto out_free_key;
	}

	desc = kmalloc(sizeof(struct shash_desc) + crypto_shash_descsize(tfm),
		       GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto out_free_hash;
	}
	desc->tfm = tfm;

	ret = crypto_shash_setkey(tfm, hashed_key, hlen);
	if (ret)
		goto out_free_desc;

	ret = crypto_shash_init(desc);
	if (ret)
		goto out_free_desc;

	ret = crypto_shash_update(desc, challenge, hlen);
	if (ret)
		goto out_free_desc;

	ret = crypto_shash_final(desc, aug);
out_free_desc:
	kfree_sensitive(desc);
out_free_hash:
	crypto_free_shash(tfm);
out_free_key:
	kfree_sensitive(hashed_key);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_augmented_challenge);

int nvme_auth_gen_privkey(struct crypto_kpp *dh_tfm, u8 dh_gid)
{
	int ret;
	struct dh dh = {0};
	size_t dh_secret_len;
	u8 *dh_secret;

	dh.group_id = nvme_auth_dhgroup_group_id(dh_gid);
	if (dh.group_id == DH_GROUP_ID_UNKNOWN) {
		pr_warn("invalid dh group %u\n", dh_gid);
		return -EINVAL;
	}

	dh_secret_len = crypto_dh_key_len(&dh);
	dh_secret = kzalloc(dh_secret_len, GFP_KERNEL);
	if (!dh_secret)
			return -ENOMEM;

	ret = crypto_dh_encode_key(dh_secret, dh_secret_len, &dh);
	if (ret) {
		pr_debug("failed to encode private key, error %d\n", ret);
		goto out;
	}
	ret = crypto_kpp_set_secret(dh_tfm, dh_secret, dh_secret_len);
	if (ret)
		pr_debug("failed to set private key, error %d\n", ret);
out:
	kfree_sensitive(dh_secret);
	dh_secret = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_privkey);

int nvme_auth_gen_pubkey(struct crypto_kpp *dh_tfm,
		u8 *host_key, size_t host_key_len)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist dst;
	int ret;

	req = kpp_request_alloc(dh_tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	kpp_request_set_input(req, NULL, 0);
	sg_init_one(&dst, host_key, host_key_len);
	kpp_request_set_output(req, &dst, host_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_generate_public_key(req), &wait);
	kpp_request_free(req);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_pubkey);

int nvme_auth_gen_shared_secret(struct crypto_kpp *dh_tfm,
		u8 *ctrl_key, size_t ctrl_key_len,
		u8 *sess_key, size_t sess_key_len)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist src, dst;
	int ret;

	req = kpp_request_alloc(dh_tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	sg_init_one(&src, ctrl_key, ctrl_key_len);
	kpp_request_set_input(req, &src, ctrl_key_len);
	sg_init_one(&dst, sess_key, sess_key_len);
	kpp_request_set_output(req, &dst, sess_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_compute_shared_secret(req), &wait);

	kpp_request_free(req);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_shared_secret);

#define nvme_auth_flags_from_qid(qid) \
	(qid == NVME_QID_ANY) ? 0 : BLK_MQ_REQ_NOWAIT | BLK_MQ_REQ_RESERVED
#define nvme_auth_queue_from_qid(ctrl, qid) \
	(qid == NVME_QID_ANY) ? (ctrl)->fabrics_q : (ctrl)->connect_q

static int nvme_auth_send(struct nvme_ctrl *ctrl, int qid,
		void *data, size_t tl)
{
	struct nvme_command cmd = {};
	blk_mq_req_flags_t flags = nvme_auth_flags_from_qid(qid);
	struct request_queue *q = nvme_auth_queue_from_qid(ctrl, qid);
	int ret;

	cmd.auth_send.opcode = nvme_fabrics_command;
	cmd.auth_send.fctype = nvme_fabrics_type_auth_send;
	cmd.auth_send.secp = NVME_AUTH_DHCHAP_PROTOCOL_IDENTIFIER;
	cmd.auth_send.spsp0 = 0x01;
	cmd.auth_send.spsp1 = 0x01;
	cmd.auth_send.tl = cpu_to_le32(tl);

	ret = __nvme_submit_sync_cmd(q, &cmd, NULL, data, tl, 0, qid,
				     0, flags);
	if (ret > 0)
		dev_warn(ctrl->device,
			"qid %d auth_send failed with status %d\n", qid, ret);
	else if (ret < 0)
		dev_err(ctrl->device,
			"qid %d auth_send failed with error %d\n", qid, ret);
	return ret;
}

static int nvme_auth_receive(struct nvme_ctrl *ctrl, int qid,
		void *buf, size_t al)
{
	struct nvme_command cmd = {};
	blk_mq_req_flags_t flags = nvme_auth_flags_from_qid(qid);
	struct request_queue *q = nvme_auth_queue_from_qid(ctrl, qid);
	int ret;

	cmd.auth_receive.opcode = nvme_fabrics_command;
	cmd.auth_receive.fctype = nvme_fabrics_type_auth_receive;
	cmd.auth_receive.secp = NVME_AUTH_DHCHAP_PROTOCOL_IDENTIFIER;
	cmd.auth_receive.spsp0 = 0x01;
	cmd.auth_receive.spsp1 = 0x01;
	cmd.auth_receive.al = cpu_to_le32(al);

	ret = __nvme_submit_sync_cmd(q, &cmd, NULL, buf, al, 0, qid,
				     0, flags);
	if (ret > 0) {
		dev_warn(ctrl->device,
			 "qid %d auth_recv failed with status %x\n", qid, ret);
		ret = -EIO;
	} else if (ret < 0) {
		dev_err(ctrl->device,
			"qid %d auth_recv failed with error %d\n", qid, ret);
	}

	return ret;
}

static int nvme_auth_receive_validate(struct nvme_ctrl *ctrl, int qid,
		struct nvmf_auth_dhchap_failure_data *data,
		u16 transaction, u8 expected_msg)
{
	dev_dbg(ctrl->device, "%s: qid %d auth_type %d auth_id %x\n",
		__func__, qid, data->auth_type, data->auth_id);

	if (data->auth_type == NVME_AUTH_COMMON_MESSAGES &&
	    data->auth_id == NVME_AUTH_DHCHAP_MESSAGE_FAILURE1) {
		return data->rescode_exp;
	}
	if (data->auth_type != NVME_AUTH_DHCHAP_MESSAGES ||
	    data->auth_id != expected_msg) {
		dev_warn(ctrl->device,
			 "qid %d invalid message %02x/%02x\n",
			 qid, data->auth_type, data->auth_id);
		return NVME_AUTH_DHCHAP_FAILURE_INCORRECT_MESSAGE;
	}
	if (le16_to_cpu(data->t_id) != transaction) {
		dev_warn(ctrl->device,
			 "qid %d invalid transaction ID %d\n",
			 qid, le16_to_cpu(data->t_id));
		return NVME_AUTH_DHCHAP_FAILURE_INCORRECT_MESSAGE;
	}
	return 0;
}

static int nvme_auth_set_dhchap_negotiate_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_negotiate_data *data = chap->buf;
	size_t size = sizeof(*data) + sizeof(union nvmf_auth_protocol);

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return -EINVAL;
	}
	memset((u8 *)chap->buf, 0, size);
	data->auth_type = NVME_AUTH_COMMON_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE;
	data->t_id = cpu_to_le16(chap->transaction);
	data->sc_c = 0; /* No secure channel concatenation */
	data->napd = 1;
	data->auth_protocol[0].dhchap.authid = NVME_AUTH_DHCHAP_AUTH_ID;
	data->auth_protocol[0].dhchap.halen = 3;
	data->auth_protocol[0].dhchap.dhlen = 6;
	data->auth_protocol[0].dhchap.idlist[0] = NVME_AUTH_HASH_SHA256;
	data->auth_protocol[0].dhchap.idlist[1] = NVME_AUTH_HASH_SHA384;
	data->auth_protocol[0].dhchap.idlist[2] = NVME_AUTH_HASH_SHA512;
	data->auth_protocol[0].dhchap.idlist[30] = NVME_AUTH_DHGROUP_NULL;
	data->auth_protocol[0].dhchap.idlist[31] = NVME_AUTH_DHGROUP_2048;
	data->auth_protocol[0].dhchap.idlist[32] = NVME_AUTH_DHGROUP_3072;
	data->auth_protocol[0].dhchap.idlist[33] = NVME_AUTH_DHGROUP_4096;
	data->auth_protocol[0].dhchap.idlist[34] = NVME_AUTH_DHGROUP_6144;
	data->auth_protocol[0].dhchap.idlist[35] = NVME_AUTH_DHGROUP_8192;

	return size;
}

static int nvme_auth_process_dhchap_challenge(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_challenge_data *data = chap->buf;
	u16 dhvlen = le16_to_cpu(data->dhvlen);
	size_t size = sizeof(*data) + data->hl + dhvlen;
	const char *gid_name = nvme_auth_dhgroup_name(data->dhgid);
	const char *hmac_name, *kpp_name;

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return NVME_SC_INVALID_FIELD;
	}

	hmac_name = nvme_auth_hmac_name(data->hashid);
	if (!hmac_name) {
		dev_warn(ctrl->device,
			 "qid %d: invalid HASH ID %d\n",
			 chap->qid, data->hashid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return NVME_SC_INVALID_FIELD;
	}

	if (chap->hash_id == data->hashid && chap->shash_tfm &&
	    !strcmp(crypto_shash_alg_name(chap->shash_tfm), hmac_name) &&
	    crypto_shash_digestsize(chap->shash_tfm) == data->hl) {
		dev_dbg(ctrl->device,
			"qid %d: reuse existing hash %s\n",
			chap->qid, hmac_name);
		goto select_kpp;
	}

	/* Reset if hash cannot be reused */
	if (chap->shash_tfm) {
		crypto_free_shash(chap->shash_tfm);
		chap->hash_id = 0;
		chap->hash_len = 0;
	}
	chap->shash_tfm = crypto_alloc_shash(hmac_name, 0,
					     CRYPTO_ALG_ALLOCATES_MEMORY);
	if (IS_ERR(chap->shash_tfm)) {
		dev_warn(ctrl->device,
			 "qid %d: failed to allocate hash %s, error %ld\n",
			 chap->qid, hmac_name, PTR_ERR(chap->shash_tfm));
		chap->shash_tfm = NULL;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return NVME_SC_AUTH_REQUIRED;
	}

	if (crypto_shash_digestsize(chap->shash_tfm) != data->hl) {
		dev_warn(ctrl->device,
			 "qid %d: invalid hash length %d\n",
			 chap->qid, data->hl);
		crypto_free_shash(chap->shash_tfm);
		chap->shash_tfm = NULL;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return NVME_SC_AUTH_REQUIRED;
	}

	/* Reset host response if the hash had been changed */
	if (chap->hash_id != data->hashid) {
		kfree(chap->host_response);
		chap->host_response = NULL;
	}

	chap->hash_id = data->hashid;
	chap->hash_len = data->hl;
	dev_dbg(ctrl->device, "qid %d: selected hash %s\n",
		chap->qid, hmac_name);

select_kpp:
	kpp_name = nvme_auth_dhgroup_kpp(data->dhgid);
	if (!kpp_name) {
		dev_warn(ctrl->device,
			 "qid %d: invalid DH group id %d\n",
			 chap->qid, data->dhgid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
		/* Leave previous dh_tfm intact */
		return NVME_SC_AUTH_REQUIRED;
	}

	/* Clear host and controller key to avoid accidental reuse */
	kfree_sensitive(chap->host_key);
	chap->host_key = NULL;
	chap->host_key_len = 0;
	kfree_sensitive(chap->ctrl_key);
	chap->ctrl_key = NULL;
	chap->ctrl_key_len = 0;

	if (chap->dhgroup_id == data->dhgid &&
	    (data->dhgid == NVME_AUTH_DHGROUP_NULL || chap->dh_tfm)) {
		dev_dbg(ctrl->device,
			"qid %d: reuse existing DH group %s\n",
			chap->qid, gid_name);
		goto skip_kpp;
	}

	/* Reset dh_tfm if it can't be reused */
	if (chap->dh_tfm) {
		crypto_free_kpp(chap->dh_tfm);
		chap->dh_tfm = NULL;
	}

	if (data->dhgid != NVME_AUTH_DHGROUP_NULL) {
		if (dhvlen == 0) {
			dev_warn(ctrl->device,
				 "qid %d: empty DH value\n",
				 chap->qid);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			return NVME_SC_INVALID_FIELD;
		}

		chap->dh_tfm = crypto_alloc_kpp(kpp_name, 0, 0);
		if (IS_ERR(chap->dh_tfm)) {
			int ret = PTR_ERR(chap->dh_tfm);

			dev_warn(ctrl->device,
				 "qid %d: error %d initializing DH group %s\n",
				 chap->qid, ret, gid_name);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			chap->dh_tfm = NULL;
			return NVME_SC_AUTH_REQUIRED;
		}
		dev_dbg(ctrl->device, "qid %d: selected DH group %s\n",
			chap->qid, gid_name);
	} else if (dhvlen != 0) {
		dev_warn(ctrl->device,
			 "qid %d: invalid DH value for NULL DH\n",
			 chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return NVME_SC_INVALID_FIELD;
	}
	chap->dhgroup_id = data->dhgid;

skip_kpp:
	chap->s1 = le32_to_cpu(data->seqnum);
	memcpy(chap->c1, data->cval, chap->hash_len);
	if (dhvlen) {
		chap->ctrl_key = kmalloc(dhvlen, GFP_KERNEL);
		if (!chap->ctrl_key) {
			chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
			return NVME_SC_AUTH_REQUIRED;
		}
		chap->ctrl_key_len = dhvlen;
		memcpy(chap->ctrl_key, data->cval + chap->hash_len,
		       dhvlen);
		dev_dbg(ctrl->device, "ctrl public key %*ph\n",
			 (int)chap->ctrl_key_len, chap->ctrl_key);
	}

	return 0;
}

static int nvme_auth_set_dhchap_reply_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_reply_data *data = chap->buf;
	size_t size = sizeof(*data);

	size += 2 * chap->hash_len;

	if (chap->host_key_len)
		size += chap->host_key_len;

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return -EINVAL;
	}

	memset(chap->buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_REPLY;
	data->t_id = cpu_to_le16(chap->transaction);
	data->hl = chap->hash_len;
	data->dhvlen = cpu_to_le16(chap->host_key_len);
	memcpy(data->rval, chap->response, chap->hash_len);
	if (ctrl->opts->dhchap_ctrl_secret) {
		get_random_bytes(chap->c2, chap->hash_len);
		data->cvalid = 1;
		chap->s2 = nvme_auth_get_seqnum();
		memcpy(data->rval + chap->hash_len, chap->c2,
		       chap->hash_len);
		dev_dbg(ctrl->device, "%s: qid %d ctrl challenge %*ph\n",
			__func__, chap->qid, (int)chap->hash_len, chap->c2);
	} else {
		memset(chap->c2, 0, chap->hash_len);
		chap->s2 = 0;
	}
	data->seqnum = cpu_to_le32(chap->s2);
	if (chap->host_key_len) {
		dev_dbg(ctrl->device, "%s: qid %d host public key %*ph\n",
			__func__, chap->qid,
			chap->host_key_len, chap->host_key);
		memcpy(data->rval + 2 * chap->hash_len, chap->host_key,
		       chap->host_key_len);
	}

	return size;
}

static int nvme_auth_process_dhchap_success1(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_success1_data *data = chap->buf;
	size_t size = sizeof(*data);

	if (ctrl->opts->dhchap_ctrl_secret)
		size += chap->hash_len;

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return NVME_SC_INVALID_FIELD;
	}

	if (data->hl != chap->hash_len) {
		dev_warn(ctrl->device,
			 "qid %d: invalid hash length %u\n",
			 chap->qid, data->hl);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return NVME_SC_INVALID_FIELD;
	}

	/* Just print out information for the admin queue */
	if (chap->qid == -1)
		dev_info(ctrl->device,
			 "qid 0: authenticated with hash %s dhgroup %s\n",
			 nvme_auth_hmac_name(chap->hash_id),
			 nvme_auth_dhgroup_name(chap->dhgroup_id));

	if (!data->rvalid)
		return 0;

	/* Validate controller response */
	if (memcmp(chap->response, data->rval, data->hl)) {
		dev_dbg(ctrl->device, "%s: qid %d ctrl response %*ph\n",
			__func__, chap->qid, (int)chap->hash_len, data->rval);
		dev_dbg(ctrl->device, "%s: qid %d host response %*ph\n",
			__func__, chap->qid, (int)chap->hash_len,
			chap->response);
		dev_warn(ctrl->device,
			 "qid %d: controller authentication failed\n",
			 chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return NVME_SC_AUTH_REQUIRED;
	}

	/* Just print out information for the admin queue */
	if (chap->qid == -1)
		dev_info(ctrl->device,
			 "qid 0: controller authenticated\n");
	return 0;
}

static int nvme_auth_set_dhchap_success2_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_success2_data *data = chap->buf;
	size_t size = sizeof(*data);

	memset(chap->buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2;
	data->t_id = cpu_to_le16(chap->transaction);

	return size;
}

static int nvme_auth_set_dhchap_failure2_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_failure_data *data = chap->buf;
	size_t size = sizeof(*data);

	memset(chap->buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_FAILURE2;
	data->t_id = cpu_to_le16(chap->transaction);
	data->rescode = NVME_AUTH_DHCHAP_FAILURE_REASON_FAILED;
	data->rescode_exp = chap->status;

	return size;
}

static int nvme_auth_dhchap_setup_host_response(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	SHASH_DESC_ON_STACK(shash, chap->shash_tfm);
	u8 buf[4], *challenge = chap->c1;
	int ret;

	dev_dbg(ctrl->device, "%s: qid %d host response seq %u transaction %d\n",
		__func__, chap->qid, chap->s1, chap->transaction);

	if (!chap->host_response) {
		chap->host_response = nvme_auth_transform_key(ctrl->host_key,
						ctrl->opts->host->nqn);
		if (IS_ERR(chap->host_response)) {
			ret = PTR_ERR(chap->host_response);
			chap->host_response = NULL;
			return ret;
		}
	} else {
		dev_dbg(ctrl->device, "%s: qid %d re-using host response\n",
			__func__, chap->qid);
	}

	ret = crypto_shash_setkey(chap->shash_tfm,
			chap->host_response, ctrl->host_key->len);
	if (ret) {
		dev_warn(ctrl->device, "qid %d: failed to set key, error %d\n",
			 chap->qid, ret);
		goto out;
	}

	if (chap->dh_tfm) {
		challenge = kmalloc(chap->hash_len, GFP_KERNEL);
		if (!challenge) {
			ret = -ENOMEM;
			goto out;
		}
		ret = nvme_auth_augmented_challenge(chap->hash_id,
						    chap->sess_key,
						    chap->sess_key_len,
						    chap->c1, challenge,
						    chap->hash_len);
		if (ret)
			goto out;
	}

	shash->tfm = chap->shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, chap->hash_len);
	if (ret)
		goto out;
	put_unaligned_le32(chap->s1, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(chap->transaction, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, sizeof(buf));
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "HostHost", 8);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->host->nqn,
				  strlen(ctrl->opts->host->nqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->subsysnqn,
			    strlen(ctrl->opts->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, chap->response);
out:
	if (challenge != chap->c1)
		kfree(challenge);
	return ret;
}

static int nvme_auth_dhchap_setup_ctrl_response(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	SHASH_DESC_ON_STACK(shash, chap->shash_tfm);
	u8 *ctrl_response;
	u8 buf[4], *challenge = chap->c2;
	int ret;

	ctrl_response = nvme_auth_transform_key(ctrl->ctrl_key,
				ctrl->opts->subsysnqn);
	if (IS_ERR(ctrl_response)) {
		ret = PTR_ERR(ctrl_response);
		return ret;
	}
	ret = crypto_shash_setkey(chap->shash_tfm,
			ctrl_response, ctrl->ctrl_key->len);
	if (ret) {
		dev_warn(ctrl->device, "qid %d: failed to set key, error %d\n",
			 chap->qid, ret);
		goto out;
	}

	if (chap->dh_tfm) {
		challenge = kmalloc(chap->hash_len, GFP_KERNEL);
		if (!challenge) {
			ret = -ENOMEM;
			goto out;
		}
		ret = nvme_auth_augmented_challenge(chap->hash_id,
						    chap->sess_key,
						    chap->sess_key_len,
						    chap->c2, challenge,
						    chap->hash_len);
		if (ret)
			goto out;
	}
	dev_dbg(ctrl->device, "%s: qid %d ctrl response seq %u transaction %d\n",
		__func__, chap->qid, chap->s2, chap->transaction);
	dev_dbg(ctrl->device, "%s: qid %d challenge %*ph\n",
		__func__, chap->qid, (int)chap->hash_len, challenge);
	dev_dbg(ctrl->device, "%s: qid %d subsysnqn %s\n",
		__func__, chap->qid, ctrl->opts->subsysnqn);
	dev_dbg(ctrl->device, "%s: qid %d hostnqn %s\n",
		__func__, chap->qid, ctrl->opts->host->nqn);
	shash->tfm = chap->shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, chap->hash_len);
	if (ret)
		goto out;
	put_unaligned_le32(chap->s2, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(chap->transaction, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, 4);
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "Controller", 10);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->subsysnqn,
				  strlen(ctrl->opts->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->host->nqn,
				  strlen(ctrl->opts->host->nqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, chap->response);
out:
	if (challenge != chap->c2)
		kfree(challenge);
	return ret;
}

int nvme_auth_generate_key(u8 *secret, struct nvme_dhchap_key **ret_key)
{
	struct nvme_dhchap_key *key;
	u8 key_hash;

	if (!secret) {
		*ret_key = NULL;
		return 0;
	}

	if (sscanf(secret, "DHHC-1:%hhd:%*s:", &key_hash) != 1)
		return -EINVAL;

	/* Pass in the secret without the 'DHHC-1:XX:' prefix */
	key = nvme_auth_extract_key(secret + 10, key_hash);
	if (IS_ERR(key)) {
		return PTR_ERR(key);
	}

	*ret_key = key;
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_generate_key);

static int nvme_auth_dhchap_exponential(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	int ret;

	if (chap->host_key && chap->host_key_len) {
		dev_dbg(ctrl->device,
			"qid %d: reusing host key\n", chap->qid);
		goto gen_sesskey;
	}
	ret = nvme_auth_gen_privkey(chap->dh_tfm, chap->dhgroup_id);
	if (ret < 0) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return ret;
	}

	chap->host_key_len = crypto_kpp_maxsize(chap->dh_tfm);

	chap->host_key = kzalloc(chap->host_key_len, GFP_KERNEL);
	if (!chap->host_key) {
		chap->host_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return -ENOMEM;
	}
	ret = nvme_auth_gen_pubkey(chap->dh_tfm,
				   chap->host_key, chap->host_key_len);
	if (ret) {
		dev_dbg(ctrl->device,
			"failed to generate public key, error %d\n", ret);
		kfree(chap->host_key);
		chap->host_key = NULL;
		chap->host_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return ret;
	}

gen_sesskey:
	chap->sess_key_len = chap->host_key_len;
	chap->sess_key = kmalloc(chap->sess_key_len, GFP_KERNEL);
	if (!chap->sess_key) {
		chap->sess_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return -ENOMEM;
	}

	ret = nvme_auth_gen_shared_secret(chap->dh_tfm,
					  chap->ctrl_key, chap->ctrl_key_len,
					  chap->sess_key, chap->sess_key_len);
	if (ret) {
		dev_dbg(ctrl->device,
			"failed to generate shared secret, error %d\n", ret);
		kfree_sensitive(chap->sess_key);
		chap->sess_key = NULL;
		chap->sess_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return ret;
	}
	dev_dbg(ctrl->device, "shared secret %*ph\n",
		(int)chap->sess_key_len, chap->sess_key);
	return 0;
}

static void __nvme_auth_reset(struct nvme_dhchap_queue_context *chap)
{
	kfree_sensitive(chap->host_response);
	chap->host_response = NULL;
	kfree_sensitive(chap->host_key);
	chap->host_key = NULL;
	chap->host_key_len = 0;
	kfree_sensitive(chap->ctrl_key);
	chap->ctrl_key = NULL;
	chap->ctrl_key_len = 0;
	kfree_sensitive(chap->sess_key);
	chap->sess_key = NULL;
	chap->sess_key_len = 0;
	chap->status = 0;
	chap->error = 0;
	chap->s1 = 0;
	chap->s2 = 0;
	chap->transaction = 0;
	memset(chap->c1, 0, sizeof(chap->c1));
	memset(chap->c2, 0, sizeof(chap->c2));
}

static void __nvme_auth_free(struct nvme_dhchap_queue_context *chap)
{
	if (chap->shash_tfm)
		crypto_free_shash(chap->shash_tfm);
	if (chap->dh_tfm)
		crypto_free_kpp(chap->dh_tfm);
	kfree_sensitive(chap->ctrl_key);
	kfree_sensitive(chap->host_key);
	kfree_sensitive(chap->sess_key);
	kfree_sensitive(chap->host_response);
	kfree(chap->buf);
	kfree(chap);
}

static void __nvme_auth_work(struct work_struct *work)
{
	struct nvme_dhchap_queue_context *chap =
		container_of(work, struct nvme_dhchap_queue_context, auth_work);
	struct nvme_ctrl *ctrl = chap->ctrl;
	size_t tl;
	int ret = 0;

	chap->transaction = ctrl->transaction++;

	/* DH-HMAC-CHAP Step 1: send negotiate */
	dev_dbg(ctrl->device, "%s: qid %d send negotiate\n",
		__func__, chap->qid);
	ret = nvme_auth_set_dhchap_negotiate_data(ctrl, chap);
	if (ret < 0) {
		chap->error = ret;
		return;
	}
	tl = ret;
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (ret) {
		chap->error = ret;
		return;
	}

	/* DH-HMAC-CHAP Step 2: receive challenge */
	dev_dbg(ctrl->device, "%s: qid %d receive challenge\n",
		__func__, chap->qid);

	memset(chap->buf, 0, chap->buf_size);
	ret = nvme_auth_receive(ctrl, chap->qid, chap->buf, chap->buf_size);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid %d failed to receive challenge, %s %d\n",
			 chap->qid, ret < 0 ? "error" : "nvme status", ret);
		chap->error = ret;
		return;
	}
	ret = nvme_auth_receive_validate(ctrl, chap->qid, chap->buf, chap->transaction,
					 NVME_AUTH_DHCHAP_MESSAGE_CHALLENGE);
	if (ret) {
		chap->status = ret;
		chap->error = NVME_SC_AUTH_REQUIRED;
		return;
	}

	ret = nvme_auth_process_dhchap_challenge(ctrl, chap);
	if (ret) {
		/* Invalid challenge parameters */
		goto fail2;
	}

	if (chap->ctrl_key_len) {
		dev_dbg(ctrl->device,
			"%s: qid %d DH exponential\n",
			__func__, chap->qid);
		ret = nvme_auth_dhchap_exponential(ctrl, chap);
		if (ret)
			goto fail2;
	}

	dev_dbg(ctrl->device, "%s: qid %d host response\n",
		__func__, chap->qid);
	ret = nvme_auth_dhchap_setup_host_response(ctrl, chap);
	if (ret)
		goto fail2;

	/* DH-HMAC-CHAP Step 3: send reply */
	dev_dbg(ctrl->device, "%s: qid %d send reply\n",
		__func__, chap->qid);
	ret = nvme_auth_set_dhchap_reply_data(ctrl, chap);
	if (ret < 0)
		goto fail2;

	tl = ret;
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (ret)
		goto fail2;

	/* DH-HMAC-CHAP Step 4: receive success1 */
	dev_dbg(ctrl->device, "%s: qid %d receive success1\n",
		__func__, chap->qid);

	memset(chap->buf, 0, chap->buf_size);
	ret = nvme_auth_receive(ctrl, chap->qid, chap->buf, chap->buf_size);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid %d failed to receive success1, %s %d\n",
			 chap->qid, ret < 0 ? "error" : "nvme status", ret);
		chap->error = ret;
		return;
	}
	ret = nvme_auth_receive_validate(ctrl, chap->qid,
					 chap->buf, chap->transaction,
					 NVME_AUTH_DHCHAP_MESSAGE_SUCCESS1);
	if (ret) {
		chap->status = ret;
		chap->error = NVME_SC_AUTH_REQUIRED;
		return;
	}

	if (ctrl->opts->dhchap_ctrl_secret) {
		dev_dbg(ctrl->device,
			"%s: qid %d controller response\n",
			__func__, chap->qid);
		ret = nvme_auth_dhchap_setup_ctrl_response(ctrl, chap);
		if (ret)
			goto fail2;
	}

	ret = nvme_auth_process_dhchap_success1(ctrl, chap);
	if (ret) {
		/* Controller authentication failed */
		goto fail2;
	}

	/* DH-HMAC-CHAP Step 5: send success2 */
	dev_dbg(ctrl->device, "%s: qid %d send success2\n",
		__func__, chap->qid);
	tl = nvme_auth_set_dhchap_success2_data(ctrl, chap);
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (!ret) {
		chap->error = 0;
		return;
	}

fail2:
	dev_dbg(ctrl->device, "%s: qid %d send failure2, status %x\n",
		__func__, chap->qid, chap->status);
	tl = nvme_auth_set_dhchap_failure2_data(ctrl, chap);
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (!ret)
		ret = -EPROTO;
	chap->error = ret;
}

int nvme_auth_negotiate(struct nvme_ctrl *ctrl, int qid)
{
	struct nvme_dhchap_queue_context *chap;

	if (!ctrl->host_key) {
		dev_warn(ctrl->device, "qid %d: no key\n", qid);
		return -ENOKEY;
	}

	mutex_lock(&ctrl->dhchap_auth_mutex);
	/* Check if the context is already queued */
	list_for_each_entry(chap, &ctrl->dhchap_auth_list, entry) {
		WARN_ON(!chap->buf);
		if (chap->qid == qid) {
			dev_dbg(ctrl->device, "qid %d: re-using context\n", qid);
			mutex_unlock(&ctrl->dhchap_auth_mutex);
			flush_work(&chap->auth_work);
			__nvme_auth_reset(chap);
			queue_work(nvme_wq, &chap->auth_work);
			return 0;
		}
	}
	chap = kzalloc(sizeof(*chap), GFP_KERNEL);
	if (!chap) {
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		return -ENOMEM;
	}
	chap->qid = qid;
	chap->ctrl = ctrl;

	/*
	 * Allocate a large enough buffer for the entire negotiation:
	 * 4k should be enough to ffdhe8192.
	 */
	chap->buf_size = 4096;
	chap->buf = kzalloc(chap->buf_size, GFP_KERNEL);
	if (!chap->buf) {
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		kfree(chap);
		return -ENOMEM;
	}

	INIT_WORK(&chap->auth_work, __nvme_auth_work);
	list_add(&chap->entry, &ctrl->dhchap_auth_list);
	mutex_unlock(&ctrl->dhchap_auth_mutex);
	queue_work(nvme_wq, &chap->auth_work);
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_negotiate);

int nvme_auth_wait(struct nvme_ctrl *ctrl, int qid)
{
	struct nvme_dhchap_queue_context *chap;
	int ret;

	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry(chap, &ctrl->dhchap_auth_list, entry) {
		if (chap->qid != qid)
			continue;
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		flush_work(&chap->auth_work);
		ret = chap->error;
		__nvme_auth_reset(chap);
		return ret;
	}
	mutex_unlock(&ctrl->dhchap_auth_mutex);
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(nvme_auth_wait);

void nvme_auth_reset(struct nvme_ctrl *ctrl)
{
	struct nvme_dhchap_queue_context *chap;

	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry(chap, &ctrl->dhchap_auth_list, entry) {
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		flush_work(&chap->auth_work);
		__nvme_auth_reset(chap);
	}
	mutex_unlock(&ctrl->dhchap_auth_mutex);
}
EXPORT_SYMBOL_GPL(nvme_auth_reset);

static void nvme_dhchap_auth_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, dhchap_auth_work);
	int ret, q;

	/* Authenticate admin queue first */
	ret = nvme_auth_negotiate(ctrl, NVME_QID_ANY);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid 0: error %d setting up authentication\n", ret);
		return;
	}
	ret = nvme_auth_wait(ctrl, NVME_QID_ANY);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid 0: authentication failed\n");
		return;
	}

	for (q = 1; q < ctrl->queue_count; q++) {
		ret = nvme_auth_negotiate(ctrl, q);
		if (ret) {
			dev_warn(ctrl->device,
				 "qid %d: error %d setting up authentication\n",
				 q, ret);
			break;
		}
	}

	/*
	 * Failure is a soft-state; credentials remain valid until
	 * the controller terminates the connection.
	 */
}

void nvme_auth_init_ctrl(struct nvme_ctrl *ctrl)
{
	INIT_LIST_HEAD(&ctrl->dhchap_auth_list);
	INIT_WORK(&ctrl->dhchap_auth_work, nvme_dhchap_auth_work);
	mutex_init(&ctrl->dhchap_auth_mutex);
	if (!ctrl->opts)
		return;
	nvme_auth_generate_key(ctrl->opts->dhchap_secret, &ctrl->host_key);
	nvme_auth_generate_key(ctrl->opts->dhchap_ctrl_secret, &ctrl->ctrl_key);
}
EXPORT_SYMBOL_GPL(nvme_auth_init_ctrl);

void nvme_auth_stop(struct nvme_ctrl *ctrl)
{
	struct nvme_dhchap_queue_context *chap = NULL, *tmp;

	cancel_work_sync(&ctrl->dhchap_auth_work);
	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry_safe(chap, tmp, &ctrl->dhchap_auth_list, entry)
		cancel_work_sync(&chap->auth_work);
	mutex_unlock(&ctrl->dhchap_auth_mutex);
}
EXPORT_SYMBOL_GPL(nvme_auth_stop);

void nvme_auth_free(struct nvme_ctrl *ctrl)
{
	struct nvme_dhchap_queue_context *chap = NULL, *tmp;

	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry_safe(chap, tmp, &ctrl->dhchap_auth_list, entry) {
		list_del_init(&chap->entry);
		flush_work(&chap->auth_work);
		__nvme_auth_free(chap);
	}
	mutex_unlock(&ctrl->dhchap_auth_mutex);
	if (ctrl->host_key) {
		nvme_auth_free_key(ctrl->host_key);
		ctrl->host_key = NULL;
	}
	if (ctrl->ctrl_key) {
		nvme_auth_free_key(ctrl->ctrl_key);
		ctrl->ctrl_key = NULL;
	}
}
EXPORT_SYMBOL_GPL(nvme_auth_free);
