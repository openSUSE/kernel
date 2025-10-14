// SPDX-License-Identifier: GPL-2.0

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

/* Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved. */

#include <linux/vmalloc.h>
#include <net/addrconf.h>
#include <rdma/erdma-abi.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

static void assemble_qbuf_mtt_for_cmd(struct erdma_mem *mem, u32 *cfg,
				      u64 *addr0, u64 *addr1)
{
	struct erdma_mtt *mtt = mem->mtt;

	if (mem->mtt_nents > ERDMA_MAX_INLINE_MTT_ENTRIES) {
		*addr0 = mtt->buf_dma;
		*cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_LEVEL_MASK,
				   ERDMA_MR_MTT_1LEVEL);
	} else {
		*addr0 = mtt->buf[0];
		memcpy(addr1, mtt->buf + 1, MTT_SIZE(mem->mtt_nents - 1));
		*cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_LEVEL_MASK,
				   ERDMA_MR_MTT_0LEVEL);
	}
}

static int create_qp_cmd(struct erdma_ucontext *uctx, struct erdma_qp *qp)
{
	struct erdma_dev *dev = to_edev(qp->ibqp.device);
	struct erdma_pd *pd = to_epd(qp->ibqp.pd);
	struct erdma_cmdq_create_qp_req req;
	struct erdma_uqp *user_qp;
	u64 resp0, resp1;
	int err;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_QP_SQ_DEPTH_MASK,
			      ilog2(qp->attrs.sq_size)) |
		   FIELD_PREP(ERDMA_CMD_CREATE_QP_QPN_MASK, QP_ID(qp));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_QP_RQ_DEPTH_MASK,
			      ilog2(qp->attrs.rq_size)) |
		   FIELD_PREP(ERDMA_CMD_CREATE_QP_PD_MASK, pd->pdn);

	if (qp->ibqp.qp_type == IB_QPT_RC)
		req.cfg2 = FIELD_PREP(ERDMA_CMD_CREATE_QP_TYPE_MASK,
				      ERDMA_QPT_RC);
	else
		req.cfg2 = FIELD_PREP(ERDMA_CMD_CREATE_QP_TYPE_MASK,
				      ERDMA_QPT_UD);

	if (rdma_is_kernel_res(&qp->ibqp.res)) {
		u32 pgsz_range = ilog2(SZ_1M) - ERDMA_HW_PAGE_SHIFT;

		req.sq_cqn_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
				   pgsz_range) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);
		req.rq_cqn_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
				   pgsz_range) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_OFFSET_MASK, 0) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK, 1) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_LEVEL_MASK,
				   ERDMA_MR_MTT_0LEVEL);
		req.rq_mtt_cfg = req.sq_mtt_cfg;

		req.rq_buf_addr = qp->kern_qp.rq_buf_dma_addr;
		req.sq_buf_addr = qp->kern_qp.sq_buf_dma_addr;
		req.sq_dbrec_dma = qp->kern_qp.sq_dbrec_dma;
		req.rq_dbrec_dma = qp->kern_qp.rq_dbrec_dma;
	} else {
		user_qp = &qp->user_qp;
		req.sq_cqn_mtt_cfg = FIELD_PREP(
			ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->sq_mem.page_size) - ERDMA_HW_PAGE_SHIFT);
		req.sq_cqn_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);

		req.rq_cqn_mtt_cfg = FIELD_PREP(
			ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->rq_mem.page_size) - ERDMA_HW_PAGE_SHIFT);
		req.rq_cqn_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg = user_qp->sq_mem.page_offset;
		req.sq_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK,
					     user_qp->sq_mem.mtt_nents);

		req.rq_mtt_cfg = user_qp->rq_mem.page_offset;
		req.rq_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK,
					     user_qp->rq_mem.mtt_nents);

		assemble_qbuf_mtt_for_cmd(&user_qp->sq_mem, &req.sq_mtt_cfg,
					  &req.sq_buf_addr, req.sq_mtt_entry);
		assemble_qbuf_mtt_for_cmd(&user_qp->rq_mem, &req.rq_mtt_cfg,
					  &req.rq_buf_addr, req.rq_mtt_entry);

		req.sq_dbrec_dma = user_qp->sq_dbrec_dma;
		req.rq_dbrec_dma = user_qp->rq_dbrec_dma;

		if (uctx->ext_db.enable) {
			req.sq_cqn_mtt_cfg |=
				FIELD_PREP(ERDMA_CMD_CREATE_QP_DB_CFG_MASK, 1);
			req.db_cfg =
				FIELD_PREP(ERDMA_CMD_CREATE_QP_SQDB_CFG_MASK,
					   uctx->ext_db.sdb_off) |
				FIELD_PREP(ERDMA_CMD_CREATE_QP_RQDB_CFG_MASK,
					   uctx->ext_db.rdb_off);
		}
	}

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), &resp0, &resp1,
				  true);
	if (!err && erdma_device_iwarp(dev))
		qp->attrs.iwarp.cookie =
			FIELD_GET(ERDMA_CMDQ_CREATE_QP_RESP_COOKIE_MASK, resp0);

	return err;
}

static int regmr_cmd(struct erdma_dev *dev, struct erdma_mr *mr)
{
	struct erdma_pd *pd = to_epd(mr->ibmr.pd);
	u32 mtt_level = ERDMA_MR_MTT_0LEVEL;
	struct erdma_cmdq_reg_mr_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_REG_MR);

	if (mr->type == ERDMA_MR_TYPE_FRMR ||
	    mr->mem.page_cnt > ERDMA_MAX_INLINE_MTT_ENTRIES) {
		if (mr->mem.mtt->continuous) {
			req.phy_addr[0] = mr->mem.mtt->buf_dma;
			mtt_level = ERDMA_MR_MTT_1LEVEL;
		} else {
			req.phy_addr[0] = mr->mem.mtt->dma_addrs[0];
			mtt_level = mr->mem.mtt->level;
		}
	} else if (mr->type != ERDMA_MR_TYPE_DMA) {
		memcpy(req.phy_addr, mr->mem.mtt->buf,
		       MTT_SIZE(mr->mem.page_cnt));
	}

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MR_VALID_MASK, mr->valid) |
		   FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, mr->ibmr.lkey & 0xFF) |
		   FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, mr->ibmr.lkey >> 8);
	req.cfg1 = FIELD_PREP(ERDMA_CMD_REGMR_PD_MASK, pd->pdn) |
		   FIELD_PREP(ERDMA_CMD_REGMR_TYPE_MASK, mr->type) |
		   FIELD_PREP(ERDMA_CMD_REGMR_RIGHT_MASK, mr->access);
	req.cfg2 = FIELD_PREP(ERDMA_CMD_REGMR_PAGESIZE_MASK,
			      ilog2(mr->mem.page_size)) |
		   FIELD_PREP(ERDMA_CMD_REGMR_MTT_LEVEL_MASK, mtt_level) |
		   FIELD_PREP(ERDMA_CMD_REGMR_MTT_CNT_MASK, mr->mem.page_cnt);

	if (mr->type == ERDMA_MR_TYPE_DMA)
		goto post_cmd;

	if (mr->type == ERDMA_MR_TYPE_NORMAL) {
		req.start_va = mr->mem.va;
		req.size = mr->mem.len;
	}

	if (!mr->mem.mtt->continuous && mr->mem.mtt->level > 1) {
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_MR_VERSION_MASK, 1);
		req.cfg2 |= FIELD_PREP(ERDMA_CMD_REGMR_MTT_PAGESIZE_MASK,
				       PAGE_SHIFT - ERDMA_HW_PAGE_SHIFT);
		req.size_h = upper_32_bits(mr->mem.len);
		req.mtt_cnt_h = mr->mem.page_cnt >> 20;
	}

post_cmd:
	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				   true);
}

static int create_cq_cmd(struct erdma_ucontext *uctx, struct erdma_cq *cq)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);
	struct erdma_cmdq_create_cq_req req;
	struct erdma_mem *mem;
	u32 page_size;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_CQ);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_CQN_MASK, cq->cqn) |
		   FIELD_PREP(ERDMA_CMD_CREATE_CQ_DEPTH_MASK, ilog2(cq->depth));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_EQN_MASK, cq->assoc_eqn);

	if (rdma_is_kernel_res(&cq->ibcq.res)) {
		page_size = SZ_32M;
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
				       ilog2(page_size) - ERDMA_HW_PAGE_SHIFT);
		req.qbuf_addr_l = lower_32_bits(cq->kern_cq.qbuf_dma_addr);
		req.qbuf_addr_h = upper_32_bits(cq->kern_cq.qbuf_dma_addr);

		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK, 1) |
			    FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_LEVEL_MASK,
				       ERDMA_MR_MTT_0LEVEL);

		req.first_page_offset = 0;
		req.cq_dbrec_dma = cq->kern_cq.dbrec_dma;
	} else {
		mem = &cq->user_cq.qbuf_mem;
		req.cfg0 |=
			FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
				   ilog2(mem->page_size) - ERDMA_HW_PAGE_SHIFT);
		if (mem->mtt_nents == 1) {
			req.qbuf_addr_l = lower_32_bits(mem->mtt->buf[0]);
			req.qbuf_addr_h = upper_32_bits(mem->mtt->buf[0]);
			req.cfg1 |=
				FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_LEVEL_MASK,
					   ERDMA_MR_MTT_0LEVEL);
		} else {
			req.qbuf_addr_l = lower_32_bits(mem->mtt->buf_dma);
			req.qbuf_addr_h = upper_32_bits(mem->mtt->buf_dma);
			req.cfg1 |=
				FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_LEVEL_MASK,
					   ERDMA_MR_MTT_1LEVEL);
		}
		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK,
				       mem->mtt_nents);

		req.first_page_offset = mem->page_offset;
		req.cq_dbrec_dma = cq->user_cq.dbrec_dma;

		if (uctx->ext_db.enable) {
			req.cfg1 |= FIELD_PREP(
				ERDMA_CMD_CREATE_CQ_MTT_DB_CFG_MASK, 1);
			req.cfg2 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_DB_CFG_MASK,
					      uctx->ext_db.cdb_off);
		}
	}

	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				   true);
}

static int erdma_alloc_idx(struct erdma_resource_cb *res_cb)
{
	int idx;
	unsigned long flags;

	spin_lock_irqsave(&res_cb->lock, flags);
	idx = find_next_zero_bit(res_cb->bitmap, res_cb->max_cap,
				 res_cb->next_alloc_idx);
	if (idx == res_cb->max_cap) {
		idx = find_first_zero_bit(res_cb->bitmap, res_cb->max_cap);
		if (idx == res_cb->max_cap) {
			res_cb->next_alloc_idx = 1;
			spin_unlock_irqrestore(&res_cb->lock, flags);
			return -ENOSPC;
		}
	}

	set_bit(idx, res_cb->bitmap);
	res_cb->next_alloc_idx = idx + 1;
	spin_unlock_irqrestore(&res_cb->lock, flags);

	return idx;
}

static inline void erdma_free_idx(struct erdma_resource_cb *res_cb, u32 idx)
{
	unsigned long flags;
	u32 used;

	spin_lock_irqsave(&res_cb->lock, flags);
	used = __test_and_clear_bit(idx, res_cb->bitmap);
	spin_unlock_irqrestore(&res_cb->lock, flags);
	WARN_ON(!used);
}

static struct rdma_user_mmap_entry *
erdma_user_mmap_entry_insert(struct erdma_ucontext *uctx, void *address,
			     u32 size, u8 mmap_flag, u64 *mmap_offset)
{
	struct erdma_user_mmap_entry *entry =
		kzalloc(sizeof(*entry), GFP_KERNEL);
	int ret;

	if (!entry)
		return NULL;

	entry->address = (u64)address;
	entry->mmap_flag = mmap_flag;

	size = PAGE_ALIGN(size);

	ret = rdma_user_mmap_entry_insert(&uctx->ibucontext, &entry->rdma_entry,
					  size);
	if (ret) {
		kfree(entry);
		return NULL;
	}

	*mmap_offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

int erdma_query_device(struct ib_device *ibdev, struct ib_device_attr *attr,
		       struct ib_udata *unused)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(attr, 0, sizeof(*attr));

	attr->max_mr_size = dev->attrs.max_mr_size;
	attr->vendor_id = PCI_VENDOR_ID_ALIBABA;
	attr->vendor_part_id = dev->pdev->device;
	attr->hw_ver = dev->pdev->revision;
	attr->max_qp = dev->attrs.max_qp - 1;
	attr->max_qp_wr = min(dev->attrs.max_send_wr, dev->attrs.max_recv_wr);
	attr->max_qp_rd_atom = dev->attrs.max_ord;
	attr->max_qp_init_rd_atom = dev->attrs.max_ird;
	attr->max_res_rd_atom = dev->attrs.max_qp * dev->attrs.max_ird;
	attr->device_cap_flags = IB_DEVICE_MEM_MGT_EXTENSIONS;
	attr->kernel_cap_flags = IBK_LOCAL_DMA_LKEY;
	ibdev->local_dma_lkey = dev->attrs.local_dma_key;
	attr->max_send_sge = dev->attrs.max_send_sge;
	attr->max_recv_sge = dev->attrs.max_recv_sge;
	attr->max_sge_rd = dev->attrs.max_sge_rd;
	attr->max_cq = dev->attrs.max_cq - 1;
	attr->max_cqe = dev->attrs.max_cqe;
	attr->max_mr = dev->attrs.max_mr;
	attr->max_pd = dev->attrs.max_pd;
	attr->max_mw = dev->attrs.max_mw;
	attr->max_fast_reg_page_list_len = ERDMA_MAX_FRMR_PA;
	attr->page_size_cap = ERDMA_PAGE_SIZE_SUPPORT;

	if (erdma_device_rocev2(dev)) {
		attr->max_pkeys = ERDMA_MAX_PKEYS;
		attr->max_ah = dev->attrs.max_ah;
	}

	if (dev->attrs.cap_flags & ERDMA_DEV_CAP_FLAGS_ATOMIC)
		attr->atomic_cap = IB_ATOMIC_GLOB;

	attr->fw_ver = dev->attrs.fw_version;

	if (dev->netdev)
		addrconf_addr_eui48((u8 *)&attr->sys_image_guid,
				    dev->netdev->dev_addr);

	return 0;
}

int erdma_query_gid(struct ib_device *ibdev, u32 port, int idx,
		    union ib_gid *gid)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(gid, 0, sizeof(*gid));
	ether_addr_copy(gid->raw, dev->attrs.peer_addr);

	return 0;
}

int erdma_query_port(struct ib_device *ibdev, u32 port,
		     struct ib_port_attr *attr)
{
	struct erdma_dev *dev = to_edev(ibdev);
	struct net_device *ndev = dev->netdev;

	memset(attr, 0, sizeof(*attr));

	if (erdma_device_iwarp(dev)) {
		attr->gid_tbl_len = 1;
	} else {
		attr->gid_tbl_len = dev->attrs.max_gid;
		attr->ip_gids = true;
		attr->pkey_tbl_len = ERDMA_MAX_PKEYS;
	}

	attr->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_DEVICE_MGMT_SUP;
	attr->max_msg_sz = -1;

	if (!ndev)
		goto out;

	ib_get_eth_speed(ibdev, port, &attr->active_speed, &attr->active_width);
	attr->max_mtu = ib_mtu_int_to_enum(ndev->mtu);
	attr->active_mtu = ib_mtu_int_to_enum(ndev->mtu);
	attr->state = ib_get_curr_port_state(ndev);

out:
	if (attr->state == IB_PORT_ACTIVE)
		attr->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	else
		attr->phys_state = IB_PORT_PHYS_STATE_DISABLED;

	return 0;
}

int erdma_get_port_immutable(struct ib_device *ibdev, u32 port,
			     struct ib_port_immutable *port_immutable)
{
	struct erdma_dev *dev = to_edev(ibdev);

	if (erdma_device_iwarp(dev)) {
		port_immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
		port_immutable->gid_tbl_len = 1;
	} else {
		port_immutable->core_cap_flags =
			RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
		port_immutable->max_mad_size = IB_MGMT_MAD_SIZE;
		port_immutable->gid_tbl_len = dev->attrs.max_gid;
		port_immutable->pkey_tbl_len = ERDMA_MAX_PKEYS;
	}

	return 0;
}

int erdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);
	int pdn;

	pdn = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_PD]);
	if (pdn < 0)
		return pdn;

	pd->pdn = pdn;

	return 0;
}

int erdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_PD], pd->pdn);

	return 0;
}

static void erdma_flush_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct erdma_qp *qp =
		container_of(dwork, struct erdma_qp, reflush_dwork);
	struct erdma_cmdq_reflush_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_REFLUSH);
	req.qpn = QP_ID(qp);
	req.sq_pi = qp->kern_qp.sq_pi;
	req.rq_pi = qp->kern_qp.rq_pi;
	erdma_post_cmd_wait(&qp->dev->cmdq, &req, sizeof(req), NULL, NULL,
			    true);
}

static int erdma_qp_validate_cap(struct erdma_dev *dev,
				 struct ib_qp_init_attr *attrs)
{
	if ((attrs->cap.max_send_wr > dev->attrs.max_send_wr) ||
	    (attrs->cap.max_recv_wr > dev->attrs.max_recv_wr) ||
	    (attrs->cap.max_send_sge > dev->attrs.max_send_sge) ||
	    (attrs->cap.max_recv_sge > dev->attrs.max_recv_sge) ||
	    (attrs->cap.max_inline_data > ERDMA_MAX_INLINE) ||
	    !attrs->cap.max_send_wr || !attrs->cap.max_recv_wr) {
		return -EINVAL;
	}

	return 0;
}

static int erdma_qp_validate_attr(struct erdma_dev *dev,
				  struct ib_qp_init_attr *attrs)
{
	if (erdma_device_iwarp(dev) && attrs->qp_type != IB_QPT_RC)
		return -EOPNOTSUPP;

	if (erdma_device_rocev2(dev) && attrs->qp_type != IB_QPT_RC &&
	    attrs->qp_type != IB_QPT_UD && attrs->qp_type != IB_QPT_GSI)
		return -EOPNOTSUPP;

	if (attrs->srq)
		return -EOPNOTSUPP;

	if (!attrs->send_cq || !attrs->recv_cq)
		return -EOPNOTSUPP;

	return 0;
}

static void free_kernel_qp(struct erdma_qp *qp)
{
	struct erdma_dev *dev = qp->dev;

	vfree(qp->kern_qp.swr_tbl);
	vfree(qp->kern_qp.rwr_tbl);

	if (qp->kern_qp.sq_buf)
		dma_free_coherent(&dev->pdev->dev,
				  qp->attrs.sq_size << SQEBB_SHIFT,
				  qp->kern_qp.sq_buf,
				  qp->kern_qp.sq_buf_dma_addr);

	if (qp->kern_qp.sq_dbrec)
		dma_pool_free(dev->db_pool, qp->kern_qp.sq_dbrec,
			      qp->kern_qp.sq_dbrec_dma);

	if (qp->kern_qp.rq_buf)
		dma_free_coherent(&dev->pdev->dev,
				  qp->attrs.rq_size << RQE_SHIFT,
				  qp->kern_qp.rq_buf,
				  qp->kern_qp.rq_buf_dma_addr);

	if (qp->kern_qp.rq_dbrec)
		dma_pool_free(dev->db_pool, qp->kern_qp.rq_dbrec,
			      qp->kern_qp.rq_dbrec_dma);
}

static int init_kernel_qp(struct erdma_dev *dev, struct erdma_qp *qp,
			  struct ib_qp_init_attr *attrs)
{
	struct erdma_kqp *kqp = &qp->kern_qp;
	int size;

	if (attrs->sq_sig_type == IB_SIGNAL_ALL_WR)
		kqp->sig_all = 1;

	kqp->sq_pi = 0;
	kqp->sq_ci = 0;
	kqp->rq_pi = 0;
	kqp->rq_ci = 0;
	kqp->hw_sq_db =
		dev->func_bar + (ERDMA_SDB_SHARED_PAGE_INDEX << PAGE_SHIFT);
	kqp->hw_rq_db = dev->func_bar + ERDMA_BAR_RQDB_SPACE_OFFSET;

	kqp->swr_tbl = vmalloc_array(qp->attrs.sq_size, sizeof(u64));
	kqp->rwr_tbl = vmalloc_array(qp->attrs.rq_size, sizeof(u64));
	if (!kqp->swr_tbl || !kqp->rwr_tbl)
		goto err_out;

	size = qp->attrs.sq_size << SQEBB_SHIFT;
	kqp->sq_buf = dma_alloc_coherent(&dev->pdev->dev, size,
					 &kqp->sq_buf_dma_addr, GFP_KERNEL);
	if (!kqp->sq_buf)
		goto err_out;

	kqp->sq_dbrec =
		dma_pool_zalloc(dev->db_pool, GFP_KERNEL, &kqp->sq_dbrec_dma);
	if (!kqp->sq_dbrec)
		goto err_out;

	size = qp->attrs.rq_size << RQE_SHIFT;
	kqp->rq_buf = dma_alloc_coherent(&dev->pdev->dev, size,
					 &kqp->rq_buf_dma_addr, GFP_KERNEL);
	if (!kqp->rq_buf)
		goto err_out;

	kqp->rq_dbrec =
		dma_pool_zalloc(dev->db_pool, GFP_KERNEL, &kqp->rq_dbrec_dma);
	if (!kqp->rq_dbrec)
		goto err_out;

	return 0;

err_out:
	free_kernel_qp(qp);
	return -ENOMEM;
}

static void erdma_fill_bottom_mtt(struct erdma_dev *dev, struct erdma_mem *mem)
{
	struct erdma_mtt *mtt = mem->mtt;
	struct ib_block_iter biter;
	u32 idx = 0;

	while (mtt->low_level)
		mtt = mtt->low_level;

	rdma_umem_for_each_dma_block(mem->umem, &biter, mem->page_size)
		mtt->buf[idx++] = rdma_block_iter_dma_address(&biter);
}

static struct erdma_mtt *erdma_create_cont_mtt(struct erdma_dev *dev,
					       size_t size)
{
	struct erdma_mtt *mtt;

	mtt = kzalloc(sizeof(*mtt), GFP_KERNEL);
	if (!mtt)
		return ERR_PTR(-ENOMEM);

	mtt->size = size;
	mtt->buf = kzalloc(mtt->size, GFP_KERNEL);
	if (!mtt->buf)
		goto err_free_mtt;

	mtt->continuous = true;
	mtt->buf_dma = dma_map_single(&dev->pdev->dev, mtt->buf, mtt->size,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->pdev->dev, mtt->buf_dma))
		goto err_free_mtt_buf;

	return mtt;

err_free_mtt_buf:
	kfree(mtt->buf);

err_free_mtt:
	kfree(mtt);

	return ERR_PTR(-ENOMEM);
}

static void erdma_unmap_page_list(struct erdma_dev *dev, dma_addr_t *pg_dma,
				  u32 npages)
{
	u32 i;

	for (i = 0; i < npages; i++)
		dma_unmap_page(&dev->pdev->dev, pg_dma[i], PAGE_SIZE,
			       DMA_TO_DEVICE);
}

static void erdma_destroy_mtt_buf_dma_addrs(struct erdma_dev *dev,
					    struct erdma_mtt *mtt)
{
	erdma_unmap_page_list(dev, mtt->dma_addrs, mtt->npages);
	vfree(mtt->dma_addrs);
}

static void erdma_destroy_scatter_mtt(struct erdma_dev *dev,
				      struct erdma_mtt *mtt)
{
	erdma_destroy_mtt_buf_dma_addrs(dev, mtt);
	vfree(mtt->buf);
	kfree(mtt);
}

static void erdma_init_middle_mtt(struct erdma_mtt *mtt,
				  struct erdma_mtt *low_mtt)
{
	dma_addr_t *pg_addr = mtt->buf;
	u32 i;

	for (i = 0; i < low_mtt->npages; i++)
		pg_addr[i] = low_mtt->dma_addrs[i];
}

static u32 vmalloc_to_dma_addrs(struct erdma_dev *dev, dma_addr_t **dma_addrs,
				void *buf, u64 len)
{
	dma_addr_t *pg_dma;
	struct page *pg;
	u32 npages, i;
	void *addr;

	npages = (PAGE_ALIGN((u64)buf + len) - PAGE_ALIGN_DOWN((u64)buf)) >>
		 PAGE_SHIFT;
	pg_dma = vcalloc(npages, sizeof(*pg_dma));
	if (!pg_dma)
		return 0;

	addr = buf;
	for (i = 0; i < npages; i++) {
		pg = vmalloc_to_page(addr);
		if (!pg)
			goto err;

		pg_dma[i] = dma_map_page(&dev->pdev->dev, pg, 0, PAGE_SIZE,
					 DMA_TO_DEVICE);
		if (dma_mapping_error(&dev->pdev->dev, pg_dma[i]))
			goto err;

		addr += PAGE_SIZE;
	}

	*dma_addrs = pg_dma;

	return npages;
err:
	erdma_unmap_page_list(dev, pg_dma, i);
	vfree(pg_dma);

	return 0;
}

static int erdma_create_mtt_buf_dma_addrs(struct erdma_dev *dev,
					  struct erdma_mtt *mtt)
{
	dma_addr_t *addrs;
	u32 npages;

	/* Failed if buf is not page aligned */
	if ((uintptr_t)mtt->buf & ~PAGE_MASK)
		return -EINVAL;

	npages = vmalloc_to_dma_addrs(dev, &addrs, mtt->buf, mtt->size);
	if (!npages)
		return -ENOMEM;

	mtt->dma_addrs = addrs;
	mtt->npages = npages;

	return 0;
}

static struct erdma_mtt *erdma_create_scatter_mtt(struct erdma_dev *dev,
						  size_t size)
{
	struct erdma_mtt *mtt;
	int ret = -ENOMEM;

	mtt = kzalloc(sizeof(*mtt), GFP_KERNEL);
	if (!mtt)
		return ERR_PTR(-ENOMEM);

	mtt->size = ALIGN(size, PAGE_SIZE);
	mtt->buf = vzalloc(mtt->size);
	mtt->continuous = false;
	if (!mtt->buf)
		goto err_free_mtt;

	ret = erdma_create_mtt_buf_dma_addrs(dev, mtt);
	if (ret)
		goto err_free_mtt_buf;

	ibdev_dbg(&dev->ibdev, "create scatter mtt, size:%lu, npages:%u\n",
		  mtt->size, mtt->npages);

	return mtt;

err_free_mtt_buf:
	vfree(mtt->buf);

err_free_mtt:
	kfree(mtt);

	return ERR_PTR(ret);
}

static struct erdma_mtt *erdma_create_mtt(struct erdma_dev *dev, size_t size,
					  bool force_continuous)
{
	struct erdma_mtt *mtt, *tmp_mtt;
	int ret, level = 0;

	ibdev_dbg(&dev->ibdev, "create_mtt, size:%lu, force cont:%d\n", size,
		  force_continuous);

	if (!(dev->attrs.cap_flags & ERDMA_DEV_CAP_FLAGS_MTT_VA))
		force_continuous = true;

	if (force_continuous)
		return erdma_create_cont_mtt(dev, size);

	mtt = erdma_create_scatter_mtt(dev, size);
	if (IS_ERR(mtt))
		return mtt;
	level = 1;

	/* convergence the mtt table. */
	while (mtt->npages != 1 && level <= 3) {
		tmp_mtt = erdma_create_scatter_mtt(dev, MTT_SIZE(mtt->npages));
		if (IS_ERR(tmp_mtt)) {
			ret = PTR_ERR(tmp_mtt);
			goto err_free_mtt;
		}
		erdma_init_middle_mtt(tmp_mtt, mtt);
		tmp_mtt->low_level = mtt;
		mtt = tmp_mtt;
		level++;
	}

	if (level > 3) {
		ret = -ENOMEM;
		goto err_free_mtt;
	}

	mtt->level = level;
	ibdev_dbg(&dev->ibdev, "top mtt: level:%d, dma_addr 0x%llx\n",
		  mtt->level, mtt->dma_addrs[0]);

	return mtt;
err_free_mtt:
	while (mtt) {
		tmp_mtt = mtt->low_level;
		erdma_destroy_scatter_mtt(dev, mtt);
		mtt = tmp_mtt;
	}

	return ERR_PTR(ret);
}

static void erdma_destroy_mtt(struct erdma_dev *dev, struct erdma_mtt *mtt)
{
	struct erdma_mtt *tmp_mtt;

	if (mtt->continuous) {
		dma_unmap_single(&dev->pdev->dev, mtt->buf_dma, mtt->size,
				 DMA_TO_DEVICE);
		kfree(mtt->buf);
		kfree(mtt);
	} else {
		while (mtt) {
			tmp_mtt = mtt->low_level;
			erdma_destroy_scatter_mtt(dev, mtt);
			mtt = tmp_mtt;
		}
	}
}

static int get_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem,
			   u64 start, u64 len, int access, u64 virt,
			   unsigned long req_page_size, bool force_continuous)
{
	int ret = 0;

	mem->umem = ib_umem_get(&dev->ibdev, start, len, access);
	if (IS_ERR(mem->umem)) {
		ret = PTR_ERR(mem->umem);
		mem->umem = NULL;
		return ret;
	}

	mem->va = virt;
	mem->len = len;
	mem->page_size = ib_umem_find_best_pgsz(mem->umem, req_page_size, virt);
	mem->page_offset = start & (mem->page_size - 1);
	mem->mtt_nents = ib_umem_num_dma_blocks(mem->umem, mem->page_size);
	mem->page_cnt = mem->mtt_nents;
	mem->mtt = erdma_create_mtt(dev, MTT_SIZE(mem->page_cnt),
				    force_continuous);
	if (IS_ERR(mem->mtt)) {
		ret = PTR_ERR(mem->mtt);
		goto error_ret;
	}

	erdma_fill_bottom_mtt(dev, mem);

	return 0;

error_ret:
	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}

	return ret;
}

static void put_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem)
{
	if (mem->mtt)
		erdma_destroy_mtt(dev, mem->mtt);

	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}
}

static int erdma_map_user_dbrecords(struct erdma_ucontext *ctx,
				    u64 dbrecords_va,
				    struct erdma_user_dbrecords_page **dbr_page,
				    dma_addr_t *dma_addr)
{
	struct erdma_user_dbrecords_page *page = NULL;
	int rv = 0;

	mutex_lock(&ctx->dbrecords_page_mutex);

	list_for_each_entry(page, &ctx->dbrecords_page_list, list)
		if (page->va == (dbrecords_va & PAGE_MASK))
			goto found;

	page = kmalloc(sizeof(*page), GFP_KERNEL);
	if (!page) {
		rv = -ENOMEM;
		goto out;
	}

	page->va = (dbrecords_va & PAGE_MASK);
	page->refcnt = 0;

	page->umem = ib_umem_get(ctx->ibucontext.device,
				 dbrecords_va & PAGE_MASK, PAGE_SIZE, 0);
	if (IS_ERR(page->umem)) {
		rv = PTR_ERR(page->umem);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &ctx->dbrecords_page_list);

found:
	*dma_addr = sg_dma_address(page->umem->sgt_append.sgt.sgl) +
		    (dbrecords_va & ~PAGE_MASK);
	*dbr_page = page;
	page->refcnt++;

out:
	mutex_unlock(&ctx->dbrecords_page_mutex);
	return rv;
}

static void
erdma_unmap_user_dbrecords(struct erdma_ucontext *ctx,
			   struct erdma_user_dbrecords_page **dbr_page)
{
	if (!ctx || !(*dbr_page))
		return;

	mutex_lock(&ctx->dbrecords_page_mutex);
	if (--(*dbr_page)->refcnt == 0) {
		list_del(&(*dbr_page)->list);
		ib_umem_release((*dbr_page)->umem);
		kfree(*dbr_page);
	}

	*dbr_page = NULL;
	mutex_unlock(&ctx->dbrecords_page_mutex);
}

static int init_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx,
			u64 va, u32 len, u64 dbrec_va)
{
	dma_addr_t dbrec_dma;
	u32 rq_offset;
	int ret;

	if (len < (ALIGN(qp->attrs.sq_size * SQEBB_SIZE, ERDMA_HW_PAGE_SIZE) +
		   qp->attrs.rq_size * RQE_SIZE))
		return -EINVAL;

	ret = get_mtt_entries(qp->dev, &qp->user_qp.sq_mem, va,
			      qp->attrs.sq_size << SQEBB_SHIFT, 0, va,
			      (SZ_1M - SZ_4K), true);
	if (ret)
		return ret;

	rq_offset = ALIGN(qp->attrs.sq_size << SQEBB_SHIFT, ERDMA_HW_PAGE_SIZE);
	qp->user_qp.rq_offset = rq_offset;

	ret = get_mtt_entries(qp->dev, &qp->user_qp.rq_mem, va + rq_offset,
			      qp->attrs.rq_size << RQE_SHIFT, 0, va + rq_offset,
			      (SZ_1M - SZ_4K), true);
	if (ret)
		goto put_sq_mtt;

	ret = erdma_map_user_dbrecords(uctx, dbrec_va,
				       &qp->user_qp.user_dbr_page,
				       &dbrec_dma);
	if (ret)
		goto put_rq_mtt;

	qp->user_qp.sq_dbrec_dma = dbrec_dma;
	qp->user_qp.rq_dbrec_dma = dbrec_dma + ERDMA_DB_SIZE;

	return 0;

put_rq_mtt:
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mem);

put_sq_mtt:
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mem);

	return ret;
}

static void free_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx)
{
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mem);
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mem);
	erdma_unmap_user_dbrecords(uctx, &qp->user_qp.user_dbr_page);
}

int erdma_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attrs,
		    struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	struct erdma_ureq_create_qp ureq;
	struct erdma_uresp_create_qp uresp;
	void *old_entry;
	int ret = 0;

	ret = erdma_qp_validate_cap(dev, attrs);
	if (ret)
		goto err_out;

	ret = erdma_qp_validate_attr(dev, attrs);
	if (ret)
		goto err_out;

	qp->scq = to_ecq(attrs->send_cq);
	qp->rcq = to_ecq(attrs->recv_cq);
	qp->dev = dev;
	qp->attrs.cc = dev->attrs.cc;

	init_rwsem(&qp->state_lock);
	kref_init(&qp->ref);
	init_completion(&qp->safe_free);

	if (qp->ibqp.qp_type == IB_QPT_GSI) {
		old_entry = xa_store(&dev->qp_xa, 1, qp, GFP_KERNEL);
		if (xa_is_err(old_entry))
			ret = xa_err(old_entry);
		else
			qp->ibqp.qp_num = 1;
	} else {
		ret = xa_alloc_cyclic(&dev->qp_xa, &qp->ibqp.qp_num, qp,
				      XA_LIMIT(1, dev->attrs.max_qp - 1),
				      &dev->next_alloc_qpn, GFP_KERNEL);
	}

	if (ret < 0) {
		ret = -ENOMEM;
		goto err_out;
	}

	qp->attrs.sq_size = roundup_pow_of_two(attrs->cap.max_send_wr *
					       ERDMA_MAX_WQEBB_PER_SQE);
	qp->attrs.rq_size = roundup_pow_of_two(attrs->cap.max_recv_wr);

	if (uctx) {
		ret = ib_copy_from_udata(&ureq, udata,
					 min(sizeof(ureq), udata->inlen));
		if (ret)
			goto err_out_xa;

		ret = init_user_qp(qp, uctx, ureq.qbuf_va, ureq.qbuf_len,
				   ureq.db_record_va);
		if (ret)
			goto err_out_xa;

		memset(&uresp, 0, sizeof(uresp));

		uresp.num_sqe = qp->attrs.sq_size;
		uresp.num_rqe = qp->attrs.rq_size;
		uresp.qp_id = QP_ID(qp);
		uresp.rq_offset = qp->user_qp.rq_offset;

		ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (ret)
			goto err_out_cmd;
	} else {
		ret = init_kernel_qp(dev, qp, attrs);
		if (ret)
			goto err_out_xa;
	}

	qp->attrs.max_send_sge = attrs->cap.max_send_sge;
	qp->attrs.max_recv_sge = attrs->cap.max_recv_sge;

	if (erdma_device_iwarp(qp->dev))
		qp->attrs.iwarp.state = ERDMA_QPS_IWARP_IDLE;
	else
		qp->attrs.rocev2.state = ERDMA_QPS_ROCEV2_RESET;

	INIT_DELAYED_WORK(&qp->reflush_dwork, erdma_flush_worker);

	ret = create_qp_cmd(uctx, qp);
	if (ret)
		goto err_out_cmd;

	spin_lock_init(&qp->lock);

	return 0;

err_out_cmd:
	if (uctx)
		free_user_qp(qp, uctx);
	else
		free_kernel_qp(qp);
err_out_xa:
	xa_erase(&dev->qp_xa, QP_ID(qp));
err_out:
	return ret;
}

static int erdma_create_stag(struct erdma_dev *dev, u32 *stag)
{
	int stag_idx;

	stag_idx = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX]);
	if (stag_idx < 0)
		return stag_idx;

	/* For now, we always let key field be zero. */
	*stag = (stag_idx << 8);

	return 0;
}

struct ib_mr *erdma_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct erdma_dev *dev = to_edev(ibpd->device);
	struct erdma_mr *mr;
	u32 stag;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_DMA;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->access = ERDMA_MR_ACC_LR | to_erdma_access_flags(acc);
	ret = regmr_cmd(dev, mr);
	if (ret)
		goto out_remove_stag;

	return &mr->ibmr;

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

struct ib_mr *erdma_ib_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
				u32 max_num_sg)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibpd->device);
	int ret;
	u32 stag;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EOPNOTSUPP);

	if (max_num_sg > ERDMA_MR_MAX_MTT_CNT)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_FRMR;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	/* update it in FRMR. */
	mr->access = ERDMA_MR_ACC_LR | ERDMA_MR_ACC_LW | ERDMA_MR_ACC_RR |
		     ERDMA_MR_ACC_RW;

	mr->mem.page_size = PAGE_SIZE; /* update it later. */
	mr->mem.page_cnt = max_num_sg;
	mr->mem.mtt = erdma_create_mtt(dev, MTT_SIZE(max_num_sg), true);
	if (IS_ERR(mr->mem.mtt)) {
		ret = PTR_ERR(mr->mem.mtt);
		goto out_remove_stag;
	}

	ret = regmr_cmd(dev, mr);
	if (ret)
		goto out_destroy_mtt;

	return &mr->ibmr;

out_destroy_mtt:
	erdma_destroy_mtt(dev, mr->mem.mtt);

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

static int erdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct erdma_mr *mr = to_emr(ibmr);

	if (mr->mem.mtt_nents >= mr->mem.page_cnt)
		return -1;

	mr->mem.mtt->buf[mr->mem.mtt_nents] = addr;
	mr->mem.mtt_nents++;

	return 0;
}

int erdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset)
{
	struct erdma_mr *mr = to_emr(ibmr);
	int num;

	mr->mem.mtt_nents = 0;

	num = ib_sg_to_pages(&mr->ibmr, sg, sg_nents, sg_offset,
			     erdma_set_page);

	return num;
}

struct ib_mr *erdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				u64 virt, int access, struct ib_dmah *dmah,
				struct ib_udata *udata)
{
	struct erdma_mr *mr = NULL;
	struct erdma_dev *dev = to_edev(ibpd->device);
	u32 stag;
	int ret;

	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);

	if (!len || len > dev->attrs.max_mr_size)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = get_mtt_entries(dev, &mr->mem, start, len, access, virt,
			      SZ_2G - SZ_4K, false);
	if (ret)
		goto err_out_free;

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto err_out_put_mtt;

	mr->ibmr.lkey = mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->mem.va = virt;
	mr->mem.len = len;
	mr->access = ERDMA_MR_ACC_LR | to_erdma_access_flags(access);
	mr->valid = 1;
	mr->type = ERDMA_MR_TYPE_NORMAL;

	ret = regmr_cmd(dev, mr);
	if (ret)
		goto err_out_mr;

	return &mr->ibmr;

err_out_mr:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

err_out_put_mtt:
	put_mtt_entries(dev, &mr->mem);

err_out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

int erdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibmr->device);
	struct erdma_cmdq_dereg_mr_req req;
	int ret;

	mr = to_emr(ibmr);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DEREG_MR);

	req.cfg = FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, ibmr->lkey >> 8) |
		  FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, ibmr->lkey & 0xFF);

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (ret)
		return ret;

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], ibmr->lkey >> 8);

	put_mtt_entries(dev, &mr->mem);

	kfree(mr);
	return 0;
}

int erdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	int err;
	struct erdma_cmdq_destroy_cq_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_CQ);
	req.cqn = cq->cqn;

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (err)
		return err;

	if (rdma_is_kernel_res(&cq->ibcq.res)) {
		dma_free_coherent(&dev->pdev->dev, cq->depth << CQE_SHIFT,
				  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
		dma_pool_free(dev->db_pool, cq->kern_cq.dbrec,
			      cq->kern_cq.dbrec_dma);
	} else {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mem);
	}

	xa_erase(&dev->cq_xa, cq->cqn);

	return 0;
}

int erdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	struct erdma_cmdq_destroy_qp_req req;
	union erdma_mod_qp_params params;
	int err;

	down_write(&qp->state_lock);
	if (erdma_device_iwarp(dev)) {
		params.iwarp.state = ERDMA_QPS_IWARP_ERROR;
		erdma_modify_qp_state_iwarp(qp, &params.iwarp,
					    ERDMA_QPA_IWARP_STATE);
	} else {
		params.rocev2.state = ERDMA_QPS_ROCEV2_ERROR;
		erdma_modify_qp_state_rocev2(qp, &params.rocev2,
					     ERDMA_QPA_ROCEV2_STATE);
	}
	up_write(&qp->state_lock);

	cancel_delayed_work_sync(&qp->reflush_dwork);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_QP);
	req.qpn = QP_ID(qp);

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (err)
		return err;

	erdma_qp_put(qp);
	wait_for_completion(&qp->safe_free);

	if (rdma_is_kernel_res(&qp->ibqp.res)) {
		free_kernel_qp(qp);
	} else {
		put_mtt_entries(dev, &qp->user_qp.sq_mem);
		put_mtt_entries(dev, &qp->user_qp.rq_mem);
		erdma_unmap_user_dbrecords(ctx, &qp->user_qp.user_dbr_page);
	}

	if (qp->cep)
		erdma_cep_put(qp->cep);
	xa_erase(&dev->qp_xa, QP_ID(qp));

	return 0;
}

void erdma_qp_get_ref(struct ib_qp *ibqp)
{
	erdma_qp_get(to_eqp(ibqp));
}

void erdma_qp_put_ref(struct ib_qp *ibqp)
{
	erdma_qp_put(to_eqp(ibqp));
}

int erdma_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct erdma_user_mmap_entry *entry;
	pgprot_t prot;
	int err;

	rdma_entry = rdma_user_mmap_entry_get(ctx, vma);
	if (!rdma_entry)
		return -EINVAL;

	entry = to_emmap(rdma_entry);

	switch (entry->mmap_flag) {
	case ERDMA_MMAP_IO_NC:
		/* map doorbell. */
		prot = pgprot_device(vma->vm_page_prot);
		break;
	default:
		err = -EINVAL;
		goto put_entry;
	}

	err = rdma_user_mmap_io(ctx, vma, PFN_DOWN(entry->address), PAGE_SIZE,
				prot, rdma_entry);

put_entry:
	rdma_user_mmap_entry_put(rdma_entry);
	return err;
}

void erdma_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct erdma_user_mmap_entry *entry = to_emmap(rdma_entry);

	kfree(entry);
}

static int alloc_db_resources(struct erdma_dev *dev, struct erdma_ucontext *ctx,
			      bool ext_db_en)
{
	struct erdma_cmdq_ext_db_req req = {};
	u64 val0, val1;
	int ret;

	/*
	 * CAP_SYS_RAWIO is required if hardware does not support extend
	 * doorbell mechanism.
	 */
	if (!ext_db_en && !capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (!ext_db_en) {
		ctx->sdb = dev->func_bar_addr + ERDMA_BAR_SQDB_SPACE_OFFSET;
		ctx->rdb = dev->func_bar_addr + ERDMA_BAR_RQDB_SPACE_OFFSET;
		ctx->cdb = dev->func_bar_addr + ERDMA_BAR_CQDB_SPACE_OFFSET;
		return 0;
	}

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_ALLOC_DB);

	req.cfg = FIELD_PREP(ERDMA_CMD_EXT_DB_CQ_EN_MASK, 1) |
		  FIELD_PREP(ERDMA_CMD_EXT_DB_RQ_EN_MASK, 1) |
		  FIELD_PREP(ERDMA_CMD_EXT_DB_SQ_EN_MASK, 1);

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), &val0, &val1,
				  true);
	if (ret)
		return ret;

	ctx->ext_db.enable = true;
	ctx->ext_db.sdb_off = ERDMA_GET(val0, ALLOC_DB_RESP_SDB);
	ctx->ext_db.rdb_off = ERDMA_GET(val0, ALLOC_DB_RESP_RDB);
	ctx->ext_db.cdb_off = ERDMA_GET(val0, ALLOC_DB_RESP_CDB);

	ctx->sdb = dev->func_bar_addr + (ctx->ext_db.sdb_off << PAGE_SHIFT);
	ctx->cdb = dev->func_bar_addr + (ctx->ext_db.rdb_off << PAGE_SHIFT);
	ctx->rdb = dev->func_bar_addr + (ctx->ext_db.cdb_off << PAGE_SHIFT);

	return 0;
}

static void free_db_resources(struct erdma_dev *dev, struct erdma_ucontext *ctx)
{
	struct erdma_cmdq_ext_db_req req = {};
	int ret;

	if (!ctx->ext_db.enable)
		return;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_FREE_DB);

	req.cfg = FIELD_PREP(ERDMA_CMD_EXT_DB_CQ_EN_MASK, 1) |
		  FIELD_PREP(ERDMA_CMD_EXT_DB_RQ_EN_MASK, 1) |
		  FIELD_PREP(ERDMA_CMD_EXT_DB_SQ_EN_MASK, 1);

	req.sdb_off = ctx->ext_db.sdb_off;
	req.rdb_off = ctx->ext_db.rdb_off;
	req.cdb_off = ctx->ext_db.cdb_off;

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (ret)
		ibdev_err_ratelimited(&dev->ibdev,
				      "free db resources failed %d", ret);
}

static void erdma_uctx_user_mmap_entries_remove(struct erdma_ucontext *uctx)
{
	rdma_user_mmap_entry_remove(uctx->sq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->rq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->cq_db_mmap_entry);
}

int erdma_alloc_ucontext(struct ib_ucontext *ibctx, struct ib_udata *udata)
{
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_dev *dev = to_edev(ibctx->device);
	int ret;
	struct erdma_uresp_alloc_ctx uresp = {};

	if (atomic_inc_return(&dev->num_ctx) > ERDMA_MAX_CONTEXT) {
		ret = -ENOMEM;
		goto err_out;
	}

	if (udata->outlen < sizeof(uresp)) {
		ret = -EINVAL;
		goto err_out;
	}

	INIT_LIST_HEAD(&ctx->dbrecords_page_list);
	mutex_init(&ctx->dbrecords_page_mutex);

	ret = alloc_db_resources(dev, ctx,
				 !!(dev->attrs.cap_flags &
				    ERDMA_DEV_CAP_FLAGS_EXTEND_DB));
	if (ret)
		goto err_out;

	ctx->sq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ctx, (void *)ctx->sdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.sdb);
	if (!ctx->sq_db_mmap_entry) {
		ret = -ENOMEM;
		goto err_free_ext_db;
	}

	ctx->rq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ctx, (void *)ctx->rdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.rdb);
	if (!ctx->rq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_put_mmap_entries;
	}

	ctx->cq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ctx, (void *)ctx->cdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.cdb);
	if (!ctx->cq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_put_mmap_entries;
	}

	uresp.dev_id = dev->pdev->device;

	ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (ret)
		goto err_put_mmap_entries;

	return 0;

err_put_mmap_entries:
	erdma_uctx_user_mmap_entries_remove(ctx);

err_free_ext_db:
	free_db_resources(dev, ctx);

err_out:
	atomic_dec(&dev->num_ctx);
	return ret;
}

void erdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct erdma_dev *dev = to_edev(ibctx->device);
	struct erdma_ucontext *ctx = to_ectx(ibctx);

	erdma_uctx_user_mmap_entries_remove(ctx);
	free_db_resources(dev, ctx);
	atomic_dec(&dev->num_ctx);
}

static void erdma_attr_to_av(const struct rdma_ah_attr *ah_attr,
			     struct erdma_av *av, u16 sport)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);

	av->port = rdma_ah_get_port_num(ah_attr);
	av->sgid_index = grh->sgid_index;
	av->hop_limit = grh->hop_limit;
	av->traffic_class = grh->traffic_class;
	av->sl = rdma_ah_get_sl(ah_attr);

	av->flow_label = grh->flow_label;
	av->udp_sport = sport;

	ether_addr_copy(av->dmac, ah_attr->roce.dmac);
	memcpy(av->dgid, grh->dgid.raw, ERDMA_ROCEV2_GID_SIZE);

	if (ipv6_addr_v4mapped((struct in6_addr *)&grh->dgid))
		av->ntype = ERDMA_NETWORK_TYPE_IPV4;
	else
		av->ntype = ERDMA_NETWORK_TYPE_IPV6;
}

static void erdma_av_to_attr(struct erdma_av *av, struct rdma_ah_attr *ah_attr)
{
	ah_attr->type = RDMA_AH_ATTR_TYPE_ROCE;

	rdma_ah_set_sl(ah_attr, av->sl);
	rdma_ah_set_port_num(ah_attr, av->port);
	rdma_ah_set_ah_flags(ah_attr, IB_AH_GRH);

	rdma_ah_set_grh(ah_attr, NULL, av->flow_label, av->sgid_index,
			av->hop_limit, av->traffic_class);
	rdma_ah_set_dgid_raw(ah_attr, av->dgid);
}

static int ib_qps_to_erdma_qps[ERDMA_PROTO_COUNT][IB_QPS_ERR + 1] = {
	[ERDMA_PROTO_IWARP] = {
		[IB_QPS_RESET] = ERDMA_QPS_IWARP_IDLE,
		[IB_QPS_INIT] = ERDMA_QPS_IWARP_IDLE,
		[IB_QPS_RTR] = ERDMA_QPS_IWARP_RTR,
		[IB_QPS_RTS] = ERDMA_QPS_IWARP_RTS,
		[IB_QPS_SQD] = ERDMA_QPS_IWARP_CLOSING,
		[IB_QPS_SQE] = ERDMA_QPS_IWARP_TERMINATE,
		[IB_QPS_ERR] = ERDMA_QPS_IWARP_ERROR,
	},
	[ERDMA_PROTO_ROCEV2] = {
		[IB_QPS_RESET] = ERDMA_QPS_ROCEV2_RESET,
		[IB_QPS_INIT] = ERDMA_QPS_ROCEV2_INIT,
		[IB_QPS_RTR] = ERDMA_QPS_ROCEV2_RTR,
		[IB_QPS_RTS] = ERDMA_QPS_ROCEV2_RTS,
		[IB_QPS_SQD] = ERDMA_QPS_ROCEV2_SQD,
		[IB_QPS_SQE] = ERDMA_QPS_ROCEV2_SQE,
		[IB_QPS_ERR] = ERDMA_QPS_ROCEV2_ERROR,
	},
};

static int erdma_qps_to_ib_qps[ERDMA_PROTO_COUNT][ERDMA_QPS_ROCEV2_COUNT] = {
	[ERDMA_PROTO_IWARP] = {
		[ERDMA_QPS_IWARP_IDLE] = IB_QPS_INIT,
		[ERDMA_QPS_IWARP_RTR] = IB_QPS_RTR,
		[ERDMA_QPS_IWARP_RTS] = IB_QPS_RTS,
		[ERDMA_QPS_IWARP_CLOSING] = IB_QPS_ERR,
		[ERDMA_QPS_IWARP_TERMINATE] = IB_QPS_ERR,
		[ERDMA_QPS_IWARP_ERROR] = IB_QPS_ERR,
	},
	[ERDMA_PROTO_ROCEV2] = {
		[ERDMA_QPS_ROCEV2_RESET] = IB_QPS_RESET,
		[ERDMA_QPS_ROCEV2_INIT] = IB_QPS_INIT,
		[ERDMA_QPS_ROCEV2_RTR] = IB_QPS_RTR,
		[ERDMA_QPS_ROCEV2_RTS] = IB_QPS_RTS,
		[ERDMA_QPS_ROCEV2_SQD] = IB_QPS_SQD,
		[ERDMA_QPS_ROCEV2_SQE] = IB_QPS_SQE,
		[ERDMA_QPS_ROCEV2_ERROR] = IB_QPS_ERR,
	},
};

static inline enum erdma_qps_iwarp ib_to_iwarp_qps(enum ib_qp_state state)
{
	return ib_qps_to_erdma_qps[ERDMA_PROTO_IWARP][state];
}

static inline enum erdma_qps_rocev2 ib_to_rocev2_qps(enum ib_qp_state state)
{
	return ib_qps_to_erdma_qps[ERDMA_PROTO_ROCEV2][state];
}

static inline enum ib_qp_state iwarp_to_ib_qps(enum erdma_qps_iwarp state)
{
	return erdma_qps_to_ib_qps[ERDMA_PROTO_IWARP][state];
}

static inline enum ib_qp_state rocev2_to_ib_qps(enum erdma_qps_rocev2 state)
{
	return erdma_qps_to_ib_qps[ERDMA_PROTO_ROCEV2][state];
}

static int erdma_check_qp_attrs(struct erdma_qp *qp, struct ib_qp_attr *attr,
				int attr_mask)
{
	enum ib_qp_state cur_state, nxt_state;
	struct erdma_dev *dev = qp->dev;
	int ret = -EINVAL;

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if ((attr_mask & IB_QP_PORT) &&
	    !rdma_is_port_valid(&dev->ibdev, attr->port_num))
		goto out;

	if (erdma_device_rocev2(dev)) {
		cur_state = (attr_mask & IB_QP_CUR_STATE) ?
				    attr->cur_qp_state :
				    rocev2_to_ib_qps(qp->attrs.rocev2.state);

		nxt_state = (attr_mask & IB_QP_STATE) ? attr->qp_state :
							cur_state;

		if (!ib_modify_qp_is_ok(cur_state, nxt_state, qp->ibqp.qp_type,
					attr_mask))
			goto out;

		if ((attr_mask & IB_QP_AV) &&
		    erdma_check_gid_attr(
			    rdma_ah_read_grh(&attr->ah_attr)->sgid_attr))
			goto out;

		if ((attr_mask & IB_QP_PKEY_INDEX) &&
		    attr->pkey_index >= ERDMA_MAX_PKEYS)
			goto out;
	}

	return 0;

out:
	return ret;
}

static void erdma_init_mod_qp_params_rocev2(
	struct erdma_qp *qp, struct erdma_mod_qp_params_rocev2 *params,
	int *erdma_attr_mask, struct ib_qp_attr *attr, int ib_attr_mask)
{
	enum erdma_qpa_mask_rocev2 to_modify_attrs = 0;
	enum erdma_qps_rocev2 cur_state, nxt_state;
	u16 udp_sport;

	if (ib_attr_mask & IB_QP_CUR_STATE)
		cur_state = ib_to_rocev2_qps(attr->cur_qp_state);
	else
		cur_state = qp->attrs.rocev2.state;

	if (ib_attr_mask & IB_QP_STATE)
		nxt_state = ib_to_rocev2_qps(attr->qp_state);
	else
		nxt_state = cur_state;

	to_modify_attrs |= ERDMA_QPA_ROCEV2_STATE;
	params->state = nxt_state;

	if (ib_attr_mask & IB_QP_QKEY) {
		to_modify_attrs |= ERDMA_QPA_ROCEV2_QKEY;
		params->qkey = attr->qkey;
	}

	if (ib_attr_mask & IB_QP_SQ_PSN) {
		to_modify_attrs |= ERDMA_QPA_ROCEV2_SQ_PSN;
		params->sq_psn = attr->sq_psn;
	}

	if (ib_attr_mask & IB_QP_RQ_PSN) {
		to_modify_attrs |= ERDMA_QPA_ROCEV2_RQ_PSN;
		params->rq_psn = attr->rq_psn;
	}

	if (ib_attr_mask & IB_QP_DEST_QPN) {
		to_modify_attrs |= ERDMA_QPA_ROCEV2_DST_QPN;
		params->dst_qpn = attr->dest_qp_num;
	}

	if (ib_attr_mask & IB_QP_AV) {
		to_modify_attrs |= ERDMA_QPA_ROCEV2_AV;
		udp_sport = rdma_get_udp_sport(attr->ah_attr.grh.flow_label,
					       QP_ID(qp), params->dst_qpn);
		erdma_attr_to_av(&attr->ah_attr, &params->av, udp_sport);
	}

	*erdma_attr_mask = to_modify_attrs;
}

int erdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	union erdma_mod_qp_params params;
	int ret = 0, erdma_attr_mask = 0;

	down_write(&qp->state_lock);

	ret = erdma_check_qp_attrs(qp, attr, attr_mask);
	if (ret)
		goto out;

	if (erdma_device_iwarp(qp->dev)) {
		if (attr_mask & IB_QP_STATE) {
			erdma_attr_mask |= ERDMA_QPA_IWARP_STATE;
			params.iwarp.state = ib_to_iwarp_qps(attr->qp_state);
		}

		ret = erdma_modify_qp_state_iwarp(qp, &params.iwarp,
						  erdma_attr_mask);
	} else {
		erdma_init_mod_qp_params_rocev2(
			qp, &params.rocev2, &erdma_attr_mask, attr, attr_mask);

		ret = erdma_modify_qp_state_rocev2(qp, &params.rocev2,
						   erdma_attr_mask);
	}

out:
	up_write(&qp->state_lock);
	return ret;
}

static enum ib_qp_state query_qp_state(struct erdma_qp *qp)
{
	if (erdma_device_iwarp(qp->dev))
		return iwarp_to_ib_qps(qp->attrs.iwarp.state);
	else
		return rocev2_to_ib_qps(qp->attrs.rocev2.state);
}

int erdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		   int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct erdma_cmdq_query_qp_req_rocev2 req;
	struct erdma_dev *dev;
	struct erdma_qp *qp;
	u64 resp0, resp1;
	int ret;

	if (ibqp && qp_attr && qp_init_attr) {
		qp = to_eqp(ibqp);
		dev = to_edev(ibqp->device);
	} else {
		return -EINVAL;
	}

	qp_attr->cap.max_inline_data = ERDMA_MAX_INLINE;
	qp_init_attr->cap.max_inline_data = ERDMA_MAX_INLINE;

	qp_attr->cap.max_send_wr = qp->attrs.sq_size;
	qp_attr->cap.max_recv_wr = qp->attrs.rq_size;
	qp_attr->cap.max_send_sge = qp->attrs.max_send_sge;
	qp_attr->cap.max_recv_sge = qp->attrs.max_recv_sge;

	qp_attr->path_mtu = ib_mtu_int_to_enum(dev->netdev->mtu);
	qp_attr->max_rd_atomic = qp->attrs.irq_size;
	qp_attr->max_dest_rd_atomic = qp->attrs.orq_size;

	qp_attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE |
				   IB_ACCESS_REMOTE_READ;

	qp_init_attr->cap = qp_attr->cap;

	if (erdma_device_rocev2(dev)) {
		/* Query hardware to get some attributes */
		erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
					CMDQ_OPCODE_QUERY_QP);
		req.qpn = QP_ID(qp);

		ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), &resp0,
					  &resp1, true);
		if (ret)
			return ret;

		qp_attr->sq_psn =
			FIELD_GET(ERDMA_CMD_QUERY_QP_RESP_SQ_PSN_MASK, resp0);
		qp_attr->rq_psn =
			FIELD_GET(ERDMA_CMD_QUERY_QP_RESP_RQ_PSN_MASK, resp0);
		qp_attr->qp_state = rocev2_to_ib_qps(FIELD_GET(
			ERDMA_CMD_QUERY_QP_RESP_QP_STATE_MASK, resp0));
		qp_attr->cur_qp_state = qp_attr->qp_state;
		qp_attr->sq_draining = FIELD_GET(
			ERDMA_CMD_QUERY_QP_RESP_SQ_DRAINING_MASK, resp0);

		qp_attr->pkey_index = 0;
		qp_attr->dest_qp_num = qp->attrs.rocev2.dst_qpn;

		if (qp->ibqp.qp_type == IB_QPT_RC)
			erdma_av_to_attr(&qp->attrs.rocev2.av,
					 &qp_attr->ah_attr);
	} else {
		qp_attr->qp_state = query_qp_state(qp);
		qp_attr->cur_qp_state = qp_attr->qp_state;
	}

	return 0;
}

static int erdma_init_user_cq(struct erdma_ucontext *ctx, struct erdma_cq *cq,
			      struct erdma_ureq_create_cq *ureq)
{
	int ret;
	struct erdma_dev *dev = to_edev(cq->ibcq.device);

	ret = get_mtt_entries(dev, &cq->user_cq.qbuf_mem, ureq->qbuf_va,
			      ureq->qbuf_len, 0, ureq->qbuf_va, SZ_64M - SZ_4K,
			      true);
	if (ret)
		return ret;

	ret = erdma_map_user_dbrecords(ctx, ureq->db_record_va,
				       &cq->user_cq.user_dbr_page,
				       &cq->user_cq.dbrec_dma);
	if (ret)
		put_mtt_entries(dev, &cq->user_cq.qbuf_mem);

	return ret;
}

static int erdma_init_kernel_cq(struct erdma_cq *cq)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);

	cq->kern_cq.qbuf =
		dma_alloc_coherent(&dev->pdev->dev, cq->depth << CQE_SHIFT,
				   &cq->kern_cq.qbuf_dma_addr, GFP_KERNEL);
	if (!cq->kern_cq.qbuf)
		return -ENOMEM;

	cq->kern_cq.dbrec = dma_pool_zalloc(dev->db_pool, GFP_KERNEL,
					    &cq->kern_cq.dbrec_dma);
	if (!cq->kern_cq.dbrec)
		goto err_out;

	spin_lock_init(&cq->kern_cq.lock);
	/* use default cqdb addr */
	cq->kern_cq.db = dev->func_bar + ERDMA_BAR_CQDB_SPACE_OFFSET;

	return 0;

err_out:
	dma_free_coherent(&dev->pdev->dev, cq->depth << CQE_SHIFT,
			  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);

	return -ENOMEM;
}

int erdma_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		    struct uverbs_attr_bundle *attrs)
{
	struct ib_udata *udata = &attrs->driver_udata;
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	unsigned int depth = attr->cqe;
	int ret;
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);

	if (depth > dev->attrs.max_cqe)
		return -EINVAL;

	depth = roundup_pow_of_two(depth);
	cq->ibcq.cqe = depth;
	cq->depth = depth;
	cq->assoc_eqn = attr->comp_vector + 1;

	ret = xa_alloc_cyclic(&dev->cq_xa, &cq->cqn, cq,
			      XA_LIMIT(1, dev->attrs.max_cq - 1),
			      &dev->next_alloc_cqn, GFP_KERNEL);
	if (ret < 0)
		return ret;

	if (!rdma_is_kernel_res(&ibcq->res)) {
		struct erdma_ureq_create_cq ureq;
		struct erdma_uresp_create_cq uresp;

		ret = ib_copy_from_udata(&ureq, udata,
					 min(udata->inlen, sizeof(ureq)));
		if (ret)
			goto err_out_xa;

		ret = erdma_init_user_cq(ctx, cq, &ureq);
		if (ret)
			goto err_out_xa;

		uresp.cq_id = cq->cqn;
		uresp.num_cqe = depth;

		ret = ib_copy_to_udata(udata, &uresp,
				       min(sizeof(uresp), udata->outlen));
		if (ret)
			goto err_free_res;
	} else {
		ret = erdma_init_kernel_cq(cq);
		if (ret)
			goto err_out_xa;
	}

	ret = create_cq_cmd(ctx, cq);
	if (ret)
		goto err_free_res;

	return 0;

err_free_res:
	if (!rdma_is_kernel_res(&ibcq->res)) {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mem);
	} else {
		dma_free_coherent(&dev->pdev->dev, depth << CQE_SHIFT,
				  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
		dma_pool_free(dev->db_pool, cq->kern_cq.dbrec,
			      cq->kern_cq.dbrec_dma);
	}

err_out_xa:
	xa_erase(&dev->cq_xa, cq->cqn);

	return ret;
}

void erdma_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

void erdma_set_mtu(struct erdma_dev *dev, u32 mtu)
{
	struct erdma_cmdq_config_mtu_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_CONF_MTU);
	req.mtu = mtu;

	erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL, true);
}

void erdma_port_event(struct erdma_dev *dev, enum ib_event_type reason)
{
	struct ib_event event;

	event.device = &dev->ibdev;
	event.element.port_num = 1;
	event.event = reason;

	ib_dispatch_event(&event);
}

enum counters {
	ERDMA_STATS_TX_REQS_CNT,
	ERDMA_STATS_TX_PACKETS_CNT,
	ERDMA_STATS_TX_BYTES_CNT,
	ERDMA_STATS_TX_DISABLE_DROP_CNT,
	ERDMA_STATS_TX_BPS_METER_DROP_CNT,
	ERDMA_STATS_TX_PPS_METER_DROP_CNT,

	ERDMA_STATS_RX_PACKETS_CNT,
	ERDMA_STATS_RX_BYTES_CNT,
	ERDMA_STATS_RX_DISABLE_DROP_CNT,
	ERDMA_STATS_RX_BPS_METER_DROP_CNT,
	ERDMA_STATS_RX_PPS_METER_DROP_CNT,

	ERDMA_STATS_MAX
};

static const struct rdma_stat_desc erdma_descs[] = {
	[ERDMA_STATS_TX_REQS_CNT].name = "tx_reqs_cnt",
	[ERDMA_STATS_TX_PACKETS_CNT].name = "tx_packets_cnt",
	[ERDMA_STATS_TX_BYTES_CNT].name = "tx_bytes_cnt",
	[ERDMA_STATS_TX_DISABLE_DROP_CNT].name = "tx_disable_drop_cnt",
	[ERDMA_STATS_TX_BPS_METER_DROP_CNT].name = "tx_bps_limit_drop_cnt",
	[ERDMA_STATS_TX_PPS_METER_DROP_CNT].name = "tx_pps_limit_drop_cnt",
	[ERDMA_STATS_RX_PACKETS_CNT].name = "rx_packets_cnt",
	[ERDMA_STATS_RX_BYTES_CNT].name = "rx_bytes_cnt",
	[ERDMA_STATS_RX_DISABLE_DROP_CNT].name = "rx_disable_drop_cnt",
	[ERDMA_STATS_RX_BPS_METER_DROP_CNT].name = "rx_bps_limit_drop_cnt",
	[ERDMA_STATS_RX_PPS_METER_DROP_CNT].name = "rx_pps_limit_drop_cnt",
};

struct rdma_hw_stats *erdma_alloc_hw_port_stats(struct ib_device *device,
						u32 port_num)
{
	return rdma_alloc_hw_stats_struct(erdma_descs, ERDMA_STATS_MAX,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int erdma_query_hw_stats(struct erdma_dev *dev,
				struct rdma_hw_stats *stats)
{
	struct erdma_cmdq_query_stats_resp *resp;
	struct erdma_cmdq_query_req req;
	dma_addr_t dma_addr;
	int err;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_GET_STATS);

	resp = dma_pool_zalloc(dev->resp_pool, GFP_KERNEL, &dma_addr);
	if (!resp)
		return -ENOMEM;

	req.target_addr = dma_addr;
	req.target_length = ERDMA_HW_RESP_SIZE;

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  true);
	if (err)
		goto out;

	if (resp->hdr.magic != ERDMA_HW_RESP_MAGIC) {
		err = -EINVAL;
		goto out;
	}

	memcpy(&stats->value[0], &resp->tx_req_cnt,
	       sizeof(u64) * stats->num_counters);

out:
	dma_pool_free(dev->resp_pool, resp, dma_addr);

	return err;
}

int erdma_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		       u32 port, int index)
{
	struct erdma_dev *dev = to_edev(ibdev);
	int ret;

	if (port == 0)
		return 0;

	ret = erdma_query_hw_stats(dev, stats);
	if (ret)
		return ret;

	return stats->num_counters;
}

enum rdma_link_layer erdma_get_link_layer(struct ib_device *ibdev, u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int erdma_set_gid(struct erdma_dev *dev, u8 op, u32 idx,
			 const union ib_gid *gid)
{
	struct erdma_cmdq_set_gid_req req;
	u8 ntype;

	req.cfg = FIELD_PREP(ERDMA_CMD_SET_GID_SGID_IDX_MASK, idx) |
		  FIELD_PREP(ERDMA_CMD_SET_GID_OP_MASK, op);

	if (op == ERDMA_SET_GID_OP_ADD) {
		if (ipv6_addr_v4mapped((struct in6_addr *)gid))
			ntype = ERDMA_NETWORK_TYPE_IPV4;
		else
			ntype = ERDMA_NETWORK_TYPE_IPV6;

		req.cfg |= FIELD_PREP(ERDMA_CMD_SET_GID_NTYPE_MASK, ntype);

		memcpy(&req.gid, gid, ERDMA_ROCEV2_GID_SIZE);
	}

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_SET_GID);
	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				   true);
}

int erdma_add_gid(const struct ib_gid_attr *attr, void **context)
{
	struct erdma_dev *dev = to_edev(attr->device);
	int ret;

	ret = erdma_check_gid_attr(attr);
	if (ret)
		return ret;

	return erdma_set_gid(dev, ERDMA_SET_GID_OP_ADD, attr->index,
			     &attr->gid);
}

int erdma_del_gid(const struct ib_gid_attr *attr, void **context)
{
	return erdma_set_gid(to_edev(attr->device), ERDMA_SET_GID_OP_DEL,
			     attr->index, NULL);
}

int erdma_query_pkey(struct ib_device *ibdev, u32 port, u16 index, u16 *pkey)
{
	if (index >= ERDMA_MAX_PKEYS)
		return -EINVAL;

	*pkey = ERDMA_DEFAULT_PKEY;
	return 0;
}

void erdma_set_av_cfg(struct erdma_av_cfg *av_cfg, struct erdma_av *av)
{
	av_cfg->cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_AV_FL_MASK, av->flow_label) |
		       FIELD_PREP(ERDMA_CMD_CREATE_AV_NTYPE_MASK, av->ntype);

	av_cfg->traffic_class = av->traffic_class;
	av_cfg->hop_limit = av->hop_limit;
	av_cfg->sl = av->sl;

	av_cfg->udp_sport = av->udp_sport;
	av_cfg->sgid_index = av->sgid_index;

	ether_addr_copy(av_cfg->dmac, av->dmac);
	memcpy(av_cfg->dgid, av->dgid, ERDMA_ROCEV2_GID_SIZE);
}

int erdma_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr,
		    struct ib_udata *udata)
{
	const struct ib_global_route *grh =
		rdma_ah_read_grh(init_attr->ah_attr);
	struct erdma_dev *dev = to_edev(ibah->device);
	struct erdma_pd *pd = to_epd(ibah->pd);
	struct erdma_ah *ah = to_eah(ibah);
	struct erdma_cmdq_create_ah_req req;
	u32 udp_sport;
	int ret;

	ret = erdma_check_gid_attr(grh->sgid_attr);
	if (ret)
		return ret;

	ret = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_AH]);
	if (ret < 0)
		return ret;

	ah->ahn = ret;

	if (grh->flow_label)
		udp_sport = rdma_flow_label_to_udp_sport(grh->flow_label);
	else
		udp_sport =
			IB_ROCE_UDP_ENCAP_VALID_PORT_MIN + (ah->ahn & 0x3FFF);

	erdma_attr_to_av(init_attr->ah_attr, &ah->av, udp_sport);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_AH);

	req.pdn = pd->pdn;
	req.ahn = ah->ahn;
	erdma_set_av_cfg(&req.av_cfg, &ah->av);

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  init_attr->flags & RDMA_CREATE_AH_SLEEPABLE);
	if (ret) {
		erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_AH], ah->ahn);
		return ret;
	}

	return 0;
}

int erdma_destroy_ah(struct ib_ah *ibah, u32 flags)
{
	struct erdma_dev *dev = to_edev(ibah->device);
	struct erdma_pd *pd = to_epd(ibah->pd);
	struct erdma_ah *ah = to_eah(ibah);
	struct erdma_cmdq_destroy_ah_req req;
	int ret;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_AH);

	req.pdn = pd->pdn;
	req.ahn = ah->ahn;

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL,
				  flags & RDMA_DESTROY_AH_SLEEPABLE);
	if (ret)
		return ret;

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_AH], ah->ahn);

	return 0;
}

int erdma_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct erdma_ah *ah = to_eah(ibah);

	memset(ah_attr, 0, sizeof(*ah_attr));
	erdma_av_to_attr(&ah->av, ah_attr);

	return 0;
}
