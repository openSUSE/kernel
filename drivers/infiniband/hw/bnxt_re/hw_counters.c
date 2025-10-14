/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Statistics
 *
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_pma.h>

#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_fp.h"
#include "qplib_rcfw.h"
#include "bnxt_re.h"
#include "hw_counters.h"

static const struct rdma_stat_desc bnxt_re_stat_descs[] = {
	[BNXT_RE_RX_PKTS].name		=  "rx_pkts",
	[BNXT_RE_RX_BYTES].name		=  "rx_bytes",
	[BNXT_RE_TX_PKTS].name		=  "tx_pkts",
	[BNXT_RE_TX_BYTES].name		=  "tx_bytes",
	[BNXT_RE_RECOVERABLE_ERRORS].name	=  "recoverable_errors",
	[BNXT_RE_TX_ERRORS].name                =  "tx_roce_errors",
	[BNXT_RE_TX_DISCARDS].name              =  "tx_roce_discards",
	[BNXT_RE_RX_ERRORS].name		=  "rx_roce_errors",
	[BNXT_RE_RX_DISCARDS].name		=  "rx_roce_discards",
	[BNXT_RE_TO_RETRANSMITS].name           =  "local_ack_timeout_err",
	[BNXT_RE_SEQ_ERR_NAKS_RCVD].name        =  "packet_seq_err",
	[BNXT_RE_MAX_RETRY_EXCEEDED].name	=  "max_retry_exceeded",
	[BNXT_RE_RNR_NAKS_RCVD].name            =  "rnr_nak_retry_err",
	[BNXT_RE_MISSING_RESP].name             =  "implied_nak_seq_err",
	[BNXT_RE_UNRECOVERABLE_ERR].name     = "unrecoverable_err",
	[BNXT_RE_BAD_RESP_ERR].name          = "bad_resp_err",
	[BNXT_RE_LOCAL_QP_OP_ERR].name       = "local_qp_op_err",
	[BNXT_RE_LOCAL_PROTECTION_ERR].name  = "local_protection_err",
	[BNXT_RE_MEM_MGMT_OP_ERR].name       = "mem_mgmt_op_err",
	[BNXT_RE_REMOTE_INVALID_REQ_ERR].name   = "req_remote_invalid_request",
	[BNXT_RE_REMOTE_ACCESS_ERR].name        = "req_remote_access_errors",
	[BNXT_RE_REMOTE_OP_ERR].name         = "remote_op_err",
	[BNXT_RE_DUP_REQ].name               = "duplicate_request",
	[BNXT_RE_RES_EXCEED_MAX].name        = "res_exceed_max",
	[BNXT_RE_RES_LENGTH_MISMATCH].name   = "resp_local_length_error",
	[BNXT_RE_RES_EXCEEDS_WQE].name       = "res_exceeds_wqe",
	[BNXT_RE_RES_OPCODE_ERR].name        = "res_opcode_err",
	[BNXT_RE_RES_RX_INVALID_RKEY].name   = "res_rx_invalid_rkey",
	[BNXT_RE_RES_RX_DOMAIN_ERR].name     = "res_rx_domain_err",
	[BNXT_RE_RES_RX_NO_PERM].name        = "res_rx_no_perm",
	[BNXT_RE_RES_RX_RANGE_ERR].name      = "res_rx_range_err",
	[BNXT_RE_RES_TX_INVALID_RKEY].name   = "res_tx_invalid_rkey",
	[BNXT_RE_RES_TX_DOMAIN_ERR].name     = "res_tx_domain_err",
	[BNXT_RE_RES_TX_NO_PERM].name        = "res_tx_no_perm",
	[BNXT_RE_RES_TX_RANGE_ERR].name      = "res_tx_range_err",
	[BNXT_RE_RES_IRRQ_OFLOW].name        = "res_irrq_oflow",
	[BNXT_RE_RES_UNSUP_OPCODE].name      = "res_unsup_opcode",
	[BNXT_RE_RES_UNALIGNED_ATOMIC].name  = "res_unaligned_atomic",
	[BNXT_RE_RES_REM_INV_ERR].name       = "res_rem_inv_err",
	[BNXT_RE_RES_MEM_ERROR].name         = "res_mem_err",
	[BNXT_RE_RES_SRQ_ERR].name           = "res_srq_err",
	[BNXT_RE_RES_CMP_ERR].name           = "res_cmp_err",
	[BNXT_RE_RES_INVALID_DUP_RKEY].name  = "res_invalid_dup_rkey",
	[BNXT_RE_RES_WQE_FORMAT_ERR].name    = "res_wqe_format_err",
	[BNXT_RE_RES_CQ_LOAD_ERR].name       = "res_cq_load_err",
	[BNXT_RE_RES_SRQ_LOAD_ERR].name      = "res_srq_load_err",
	[BNXT_RE_RES_TX_PCI_ERR].name        = "res_tx_pci_err",
	[BNXT_RE_RES_RX_PCI_ERR].name        = "res_rx_pci_err",
	[BNXT_RE_OUT_OF_SEQ_ERR].name        = "out_of_sequence",
	[BNXT_RE_TX_ATOMIC_REQ].name	     = "tx_atomic_req",
	[BNXT_RE_TX_READ_REQ].name	     = "tx_read_req",
	[BNXT_RE_TX_READ_RES].name	     = "tx_read_resp",
	[BNXT_RE_TX_WRITE_REQ].name	     = "tx_write_req",
	[BNXT_RE_TX_SEND_REQ].name	     = "tx_send_req",
	[BNXT_RE_TX_ROCE_PKTS].name          = "tx_roce_only_pkts",
	[BNXT_RE_TX_ROCE_BYTES].name         = "tx_roce_only_bytes",
	[BNXT_RE_RX_ATOMIC_REQ].name	     = "rx_atomic_requests",
	[BNXT_RE_RX_READ_REQ].name	     = "rx_read_requests",
	[BNXT_RE_RX_READ_RESP].name	     = "rx_read_resp",
	[BNXT_RE_RX_WRITE_REQ].name	     = "rx_write_requests",
	[BNXT_RE_RX_SEND_REQ].name	     = "rx_send_req",
	[BNXT_RE_RX_ROCE_PKTS].name          = "rx_roce_only_pkts",
	[BNXT_RE_RX_ROCE_BYTES].name         = "rx_roce_only_bytes",
	[BNXT_RE_RX_ROCE_GOOD_PKTS].name     = "rx_roce_good_pkts",
	[BNXT_RE_RX_ROCE_GOOD_BYTES].name    = "rx_roce_good_bytes",
	[BNXT_RE_OOB].name		     = "out_of_buffer",
	[BNXT_RE_TX_CNP].name                = "np_cnp_pkts",
	[BNXT_RE_RX_CNP].name                = "rp_cnp_handled",
	[BNXT_RE_RX_ECN].name                = "np_ecn_marked_roce_packets",
	[BNXT_RE_REQ_CQE_ERROR].name            = "req_cqe_error",
	[BNXT_RE_RESP_CQE_ERROR].name           = "resp_cqe_error",
	[BNXT_RE_RESP_REMOTE_ACCESS_ERRS].name  = "resp_remote_access_errors",
};

static void bnxt_re_copy_ext_stats(struct bnxt_re_dev *rdev,
				   struct rdma_hw_stats *stats,
				   struct bnxt_qplib_ext_stat *s)
{
	stats->value[BNXT_RE_TX_ATOMIC_REQ] = s->tx_atomic_req;
	stats->value[BNXT_RE_TX_READ_REQ]   = s->tx_read_req;
	stats->value[BNXT_RE_TX_READ_RES]   = s->tx_read_res;
	stats->value[BNXT_RE_TX_WRITE_REQ]  = s->tx_write_req;
	stats->value[BNXT_RE_TX_SEND_REQ]   = s->tx_send_req;
	stats->value[BNXT_RE_TX_ROCE_PKTS]  = s->tx_roce_pkts;
	stats->value[BNXT_RE_TX_ROCE_BYTES] = s->tx_roce_bytes;
	stats->value[BNXT_RE_RX_ATOMIC_REQ] = s->rx_atomic_req;
	stats->value[BNXT_RE_RX_READ_REQ]   = s->rx_read_req;
	stats->value[BNXT_RE_RX_READ_RESP]  = s->rx_read_res;
	stats->value[BNXT_RE_RX_WRITE_REQ]  = s->rx_write_req;
	stats->value[BNXT_RE_RX_SEND_REQ]   = s->rx_send_req;
	stats->value[BNXT_RE_RX_ROCE_PKTS]  = s->rx_roce_pkts;
	stats->value[BNXT_RE_RX_ROCE_BYTES] = s->rx_roce_bytes;
	stats->value[BNXT_RE_RX_ROCE_GOOD_PKTS] = s->rx_roce_good_pkts;
	stats->value[BNXT_RE_RX_ROCE_GOOD_BYTES] = s->rx_roce_good_bytes;
	stats->value[BNXT_RE_OOB] = s->rx_out_of_buffer;
	stats->value[BNXT_RE_TX_CNP] = s->tx_cnp;
	stats->value[BNXT_RE_RX_CNP] = s->rx_cnp;
	stats->value[BNXT_RE_RX_ECN] = s->rx_ecn_marked;
	stats->value[BNXT_RE_OUT_OF_SEQ_ERR] = s->rx_out_of_sequence;
}

static int bnxt_re_get_ext_stat(struct bnxt_re_dev *rdev,
				struct rdma_hw_stats *stats)
{
	struct bnxt_qplib_ext_stat *estat = &rdev->stats.rstat.ext_stat;
	u32 fid;
	int rc;

	fid = PCI_FUNC(rdev->en_dev->pdev->devfn);
	rc = bnxt_qplib_qext_stat(&rdev->rcfw, fid, estat);
	if (rc)
		goto done;
	bnxt_re_copy_ext_stats(rdev, stats, estat);

done:
	return rc;
}

static void bnxt_re_copy_err_stats(struct bnxt_re_dev *rdev,
				   struct rdma_hw_stats *stats,
				   struct bnxt_qplib_roce_stats *err_s)
{
	stats->value[BNXT_RE_TO_RETRANSMITS] =
				err_s->to_retransmits;
	stats->value[BNXT_RE_SEQ_ERR_NAKS_RCVD] =
				err_s->seq_err_naks_rcvd;
	stats->value[BNXT_RE_MAX_RETRY_EXCEEDED] =
				err_s->max_retry_exceeded;
	stats->value[BNXT_RE_RNR_NAKS_RCVD] =
				err_s->rnr_naks_rcvd;
	stats->value[BNXT_RE_MISSING_RESP] =
				err_s->missing_resp;
	stats->value[BNXT_RE_UNRECOVERABLE_ERR] =
				err_s->unrecoverable_err;
	stats->value[BNXT_RE_BAD_RESP_ERR] =
				err_s->bad_resp_err;
	stats->value[BNXT_RE_LOCAL_QP_OP_ERR]	=
			err_s->local_qp_op_err;
	stats->value[BNXT_RE_LOCAL_PROTECTION_ERR] =
			err_s->local_protection_err;
	stats->value[BNXT_RE_MEM_MGMT_OP_ERR] =
			err_s->mem_mgmt_op_err;
	stats->value[BNXT_RE_REMOTE_INVALID_REQ_ERR] =
			err_s->remote_invalid_req_err;
	stats->value[BNXT_RE_REMOTE_ACCESS_ERR] =
			err_s->remote_access_err;
	stats->value[BNXT_RE_REMOTE_OP_ERR] =
			err_s->remote_op_err;
	stats->value[BNXT_RE_DUP_REQ] =
			err_s->dup_req;
	stats->value[BNXT_RE_RES_EXCEED_MAX] =
			err_s->res_exceed_max;
	stats->value[BNXT_RE_RES_LENGTH_MISMATCH] =
			err_s->res_length_mismatch;
	stats->value[BNXT_RE_RES_EXCEEDS_WQE] =
			err_s->res_exceeds_wqe;
	stats->value[BNXT_RE_RES_OPCODE_ERR] =
			err_s->res_opcode_err;
	stats->value[BNXT_RE_RES_RX_INVALID_RKEY] =
			err_s->res_rx_invalid_rkey;
	stats->value[BNXT_RE_RES_RX_DOMAIN_ERR] =
			err_s->res_rx_domain_err;
	stats->value[BNXT_RE_RES_RX_NO_PERM] =
			err_s->res_rx_no_perm;
	stats->value[BNXT_RE_RES_RX_RANGE_ERR]  =
			err_s->res_rx_range_err;
	stats->value[BNXT_RE_RES_TX_INVALID_RKEY] =
			err_s->res_tx_invalid_rkey;
	stats->value[BNXT_RE_RES_TX_DOMAIN_ERR] =
			err_s->res_tx_domain_err;
	stats->value[BNXT_RE_RES_TX_NO_PERM] =
			err_s->res_tx_no_perm;
	stats->value[BNXT_RE_RES_TX_RANGE_ERR]  =
			err_s->res_tx_range_err;
	stats->value[BNXT_RE_RES_IRRQ_OFLOW] =
			err_s->res_irrq_oflow;
	stats->value[BNXT_RE_RES_UNSUP_OPCODE]  =
			err_s->res_unsup_opcode;
	stats->value[BNXT_RE_RES_UNALIGNED_ATOMIC] =
			err_s->res_unaligned_atomic;
	stats->value[BNXT_RE_RES_REM_INV_ERR]   =
			err_s->res_rem_inv_err;
	stats->value[BNXT_RE_RES_MEM_ERROR] =
			err_s->res_mem_error;
	stats->value[BNXT_RE_RES_SRQ_ERR] =
			err_s->res_srq_err;
	stats->value[BNXT_RE_RES_CMP_ERR] =
			err_s->res_cmp_err;
	stats->value[BNXT_RE_RES_INVALID_DUP_RKEY] =
			err_s->res_invalid_dup_rkey;
	stats->value[BNXT_RE_RES_WQE_FORMAT_ERR] =
			err_s->res_wqe_format_err;
	stats->value[BNXT_RE_RES_CQ_LOAD_ERR]   =
			err_s->res_cq_load_err;
	stats->value[BNXT_RE_RES_SRQ_LOAD_ERR]  =
			err_s->res_srq_load_err;
	stats->value[BNXT_RE_RES_TX_PCI_ERR]    =
			err_s->res_tx_pci_err;
	stats->value[BNXT_RE_RES_RX_PCI_ERR]    =
			err_s->res_rx_pci_err;
	stats->value[BNXT_RE_OUT_OF_SEQ_ERR]    =
			err_s->res_oos_drop_count;
	stats->value[BNXT_RE_REQ_CQE_ERROR]     =
			err_s->bad_resp_err +
			err_s->local_qp_op_err +
			err_s->local_protection_err +
			err_s->mem_mgmt_op_err +
			err_s->remote_invalid_req_err +
			err_s->remote_access_err +
			err_s->remote_op_err;
	stats->value[BNXT_RE_RESP_CQE_ERROR] =
			err_s->res_cmp_err +
			err_s->res_cq_load_err;
	stats->value[BNXT_RE_RESP_REMOTE_ACCESS_ERRS] =
			err_s->res_rx_no_perm +
			err_s->res_tx_no_perm;
}

int bnxt_re_assign_pma_port_ext_counters(struct bnxt_re_dev *rdev, struct ib_mad *out_mad)
{
	struct ib_pma_portcounters_ext *pma_cnt_ext;
	struct bnxt_qplib_ext_stat *estat = &rdev->stats.rstat.ext_stat;
	struct ctx_hw_stats *hw_stats = NULL;
	int rc;

	hw_stats = rdev->qplib_ctx.stats.dma;

	pma_cnt_ext = (struct ib_pma_portcounters_ext *)(out_mad->data + 40);
	if (_is_ext_stats_supported(rdev->dev_attr->dev_cap_flags)) {
		u32 fid = PCI_FUNC(rdev->en_dev->pdev->devfn);

		rc = bnxt_qplib_qext_stat(&rdev->rcfw, fid, estat);
		if (rc)
			return rc;
	}

	pma_cnt_ext = (struct ib_pma_portcounters_ext *)(out_mad->data + 40);
	if ((bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx) && rdev->is_virtfn) ||
	    !bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx)) {
		pma_cnt_ext->port_xmit_data =
			cpu_to_be64(le64_to_cpu(hw_stats->tx_ucast_bytes) / 4);
		pma_cnt_ext->port_rcv_data =
			cpu_to_be64(le64_to_cpu(hw_stats->rx_ucast_bytes) / 4);
		pma_cnt_ext->port_xmit_packets =
			cpu_to_be64(le64_to_cpu(hw_stats->tx_ucast_pkts));
		pma_cnt_ext->port_rcv_packets =
			cpu_to_be64(le64_to_cpu(hw_stats->rx_ucast_pkts));
		pma_cnt_ext->port_unicast_rcv_packets =
			cpu_to_be64(le64_to_cpu(hw_stats->rx_ucast_pkts));
		pma_cnt_ext->port_unicast_xmit_packets =
			cpu_to_be64(le64_to_cpu(hw_stats->tx_ucast_pkts));

	} else {
		pma_cnt_ext->port_rcv_packets = cpu_to_be64(estat->rx_roce_good_pkts);
		pma_cnt_ext->port_rcv_data = cpu_to_be64(estat->rx_roce_good_bytes / 4);
		pma_cnt_ext->port_xmit_packets = cpu_to_be64(estat->tx_roce_pkts);
		pma_cnt_ext->port_xmit_data = cpu_to_be64(estat->tx_roce_bytes / 4);
		pma_cnt_ext->port_unicast_rcv_packets = cpu_to_be64(estat->rx_roce_good_pkts);
		pma_cnt_ext->port_unicast_xmit_packets = cpu_to_be64(estat->tx_roce_pkts);
	}
	return 0;
}

int bnxt_re_assign_pma_port_counters(struct bnxt_re_dev *rdev, struct ib_mad *out_mad)
{
	struct bnxt_qplib_ext_stat *estat = &rdev->stats.rstat.ext_stat;
	struct ib_pma_portcounters *pma_cnt;
	struct ctx_hw_stats *hw_stats = NULL;
	int rc;

	hw_stats = rdev->qplib_ctx.stats.dma;

	pma_cnt = (struct ib_pma_portcounters *)(out_mad->data + 40);
	if (_is_ext_stats_supported(rdev->dev_attr->dev_cap_flags)) {
		u32 fid = PCI_FUNC(rdev->en_dev->pdev->devfn);

		rc = bnxt_qplib_qext_stat(&rdev->rcfw, fid, estat);
		if (rc)
			return rc;
	}
	if ((bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx) && rdev->is_virtfn) ||
	    !bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx)) {
		pma_cnt->port_rcv_packets =
			cpu_to_be32((u32)(le64_to_cpu(hw_stats->rx_ucast_pkts)) & 0xFFFFFFFF);
		pma_cnt->port_rcv_data =
			cpu_to_be32((u32)((le64_to_cpu(hw_stats->rx_ucast_bytes) &
					   0xFFFFFFFF) / 4));
		pma_cnt->port_xmit_packets =
			cpu_to_be32((u32)(le64_to_cpu(hw_stats->tx_ucast_pkts)) & 0xFFFFFFFF);
		pma_cnt->port_xmit_data =
			cpu_to_be32((u32)((le64_to_cpu(hw_stats->tx_ucast_bytes)
					   & 0xFFFFFFFF) / 4));
	} else {
		pma_cnt->port_rcv_packets = cpu_to_be32(estat->rx_roce_good_pkts);
		pma_cnt->port_rcv_data = cpu_to_be32((estat->rx_roce_good_bytes / 4));
		pma_cnt->port_xmit_packets = cpu_to_be32(estat->tx_roce_pkts);
		pma_cnt->port_xmit_data = cpu_to_be32((estat->tx_roce_bytes / 4));
	}
	pma_cnt->port_rcv_constraint_errors = (u8)(le64_to_cpu(hw_stats->rx_discard_pkts) & 0xFF);
	pma_cnt->port_rcv_errors = cpu_to_be16((u16)(le64_to_cpu(hw_stats->rx_error_pkts)
						     & 0xFFFF));
	pma_cnt->port_xmit_constraint_errors = (u8)(le64_to_cpu(hw_stats->tx_error_pkts) & 0xFF);
	pma_cnt->port_xmit_discards = cpu_to_be16((u16)(le64_to_cpu(hw_stats->tx_discard_pkts)
							& 0xFFFF));

	return 0;
}

int bnxt_re_ib_get_hw_stats(struct ib_device *ibdev,
			    struct rdma_hw_stats *stats,
			    u32 port, int index)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	struct bnxt_qplib_roce_stats *err_s = NULL;
	struct ctx_hw_stats *hw_stats = NULL;
	int rc  = 0;

	hw_stats = rdev->qplib_ctx.stats.dma;
	if (!port || !stats)
		return -EINVAL;

	if (hw_stats) {
		stats->value[BNXT_RE_RECOVERABLE_ERRORS] =
			le64_to_cpu(hw_stats->tx_bcast_pkts);
		stats->value[BNXT_RE_TX_DISCARDS] =
			le64_to_cpu(hw_stats->tx_discard_pkts);
		stats->value[BNXT_RE_TX_ERRORS] =
			le64_to_cpu(hw_stats->tx_error_pkts);
		stats->value[BNXT_RE_RX_ERRORS] =
			le64_to_cpu(hw_stats->rx_error_pkts);
		stats->value[BNXT_RE_RX_DISCARDS] =
			le64_to_cpu(hw_stats->rx_discard_pkts);
		stats->value[BNXT_RE_RX_PKTS] =
			le64_to_cpu(hw_stats->rx_ucast_pkts);
		stats->value[BNXT_RE_RX_BYTES] =
			le64_to_cpu(hw_stats->rx_ucast_bytes);
		stats->value[BNXT_RE_TX_PKTS] =
			le64_to_cpu(hw_stats->tx_ucast_pkts);
		stats->value[BNXT_RE_TX_BYTES] =
			le64_to_cpu(hw_stats->tx_ucast_bytes);
	}
	err_s = &rdev->stats.rstat.errs;
	if (test_bit(BNXT_RE_FLAG_ISSUE_ROCE_STATS, &rdev->flags)) {
		rc = bnxt_qplib_get_roce_stats(&rdev->rcfw, err_s);
		if (rc) {
			clear_bit(BNXT_RE_FLAG_ISSUE_ROCE_STATS,
				  &rdev->flags);
			goto done;
		}
		bnxt_re_copy_err_stats(rdev, stats, err_s);
		if (bnxt_ext_stats_supported(rdev->chip_ctx, rdev->dev_attr->dev_cap_flags,
					     rdev->is_virtfn)) {
			rc = bnxt_re_get_ext_stat(rdev, stats);
			if (rc) {
				clear_bit(BNXT_RE_FLAG_ISSUE_ROCE_STATS,
					  &rdev->flags);
				goto done;
			}
		}
	}

done:
	return bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx) ?
		BNXT_RE_NUM_EXT_COUNTERS : BNXT_RE_NUM_STD_COUNTERS;
}

struct rdma_hw_stats *bnxt_re_ib_alloc_hw_port_stats(struct ib_device *ibdev,
						     u32 port_num)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	int num_counters = 0;

	if (bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx))
		num_counters = BNXT_RE_NUM_EXT_COUNTERS;
	else
		num_counters = BNXT_RE_NUM_STD_COUNTERS;

	return rdma_alloc_hw_stats_struct(bnxt_re_stat_descs, num_counters,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}
