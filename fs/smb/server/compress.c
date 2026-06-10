// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SMB2 compression support for ksmbd.
 *
 * Receive and send SMB 3.1.1 compression transforms using the common helpers.
 *
 * Copyright (C) 2026 Namjae Jeon <linkinjeon@kernel.org>
 */
#include <linux/slab.h>

#include "compress.h"
#include "smb_common.h"

/**
 * ksmbd_decompress_request() - replace a compressed request with its SMB2 PDU
 * @conn: connection which owns the current RFC1002 request buffer
 *
 * Derive the uncompressed size from the transform variant, enforce ksmbd's
 * normal message limits, and ask the common decoder to validate every payload.
 * On success, replace conn->request_buf with a regular RFC1002-framed SMB2
 * message so the rest of the request path needs no compression awareness.
 *
 * Return: 0 on success, otherwise a negative errno.
 */
int ksmbd_decompress_request(struct ksmbd_conn *conn)
{
	struct smb2_compression_hdr *hdr;
	unsigned int pdu_size = get_rfc1002_len(conn->request_buf);
	u32 orig_size, offset, out_size;
	u32 max_allowed_pdu_size;
	char *buf, *out;
	int rc;

	if (pdu_size < sizeof(struct smb2_compression_hdr))
		return -EINVAL;

	if (conn->dialect != SMB311_PROT_ID ||
	    conn->compress_algorithm == SMB3_COMPRESS_NONE)
		return -EINVAL;

	hdr = smb_get_msg(conn->request_buf);
	if (hdr->ProtocolId != SMB2_COMPRESSION_TRANSFORM_ID)
		return -EINVAL;

	orig_size = le32_to_cpu(hdr->OriginalCompressedSegmentSize);
	if (hdr->Flags == cpu_to_le16(SMB2_COMPRESSION_FLAG_CHAINED)) {
		out_size = orig_size;
	} else {
		offset = le32_to_cpu(hdr->Offset);
		if (offset > pdu_size - sizeof(*hdr) ||
		    check_add_overflow(orig_size, offset, &out_size))
			return -EINVAL;
	}

	max_allowed_pdu_size = SMB3_MAX_MSGSIZE + conn->vals->max_write_size;
	if (out_size > max_allowed_pdu_size ||
	    out_size > MAX_STREAM_PROT_LEN)
		return -EINVAL;

	out = kvmalloc(out_size + 4 + 1, KSMBD_DEFAULT_GFP);
	if (!out)
		return -ENOMEM;

	buf = (char *)hdr;
	*(__be32 *)out = cpu_to_be32(out_size);
	rc = smb_compression_decompress(conn->compress_algorithm,
					conn->compress_chained,
					buf, pdu_size, out + 4, out_size);
	if (rc) {
		kvfree(out);
		return rc;
	}

	kvfree(conn->request_buf);
	conn->request_buf = out;
	return 0;
}
