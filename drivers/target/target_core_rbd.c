/*******************************************************************************
 * Filename:  target_core_rbd.c
 *
 * This file contains the Storage Engine  <-> Ceph RBD transport
 * specific functions.
 *
 * [Was based off of target_core_iblock.c from
 *  Nicholas A. Bellinger <nab@kernel.org>]
 *
 * (c) Copyright 2003-2013 Datera, Inc.
 * (c) Copyright 2015 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/genhd.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <asm/unaligned.h>

#include <scsi/scsi_proto.h>

#include <linux/ceph/libceph.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/librbd.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_pr.h"
#include "target_core_rbd.h"

static inline struct tcm_rbd_dev *TCM_RBD_DEV(struct se_device *dev)
{
	return container_of(dev, struct tcm_rbd_dev, dev);
}

static int tcm_rbd_attach_hba(struct se_hba *hba, u32 host_id)
{
	pr_debug("CORE_HBA[%d] - TCM RBD HBA Driver %s on"
		" Generic Target Core Stack %s\n", hba->hba_id,
		TCM_RBD_VERSION, TARGET_CORE_VERSION);
	return 0;
}

static void tcm_rbd_detach_hba(struct se_hba *hba)
{
}

static struct se_device *tcm_rbd_alloc_device(struct se_hba *hba,
					      const char *name)
{
	struct tcm_rbd_dev *tcm_rbd_dev;

	tcm_rbd_dev = kzalloc(sizeof(struct tcm_rbd_dev), GFP_KERNEL);
	if (!tcm_rbd_dev) {
		pr_err("Unable to allocate struct tcm_rbd_dev\n");
		return NULL;
	}

	pr_debug( "TCM RBD: Allocated tcm_rbd_dev for %s\n", name);

	return &tcm_rbd_dev->dev;
}

static int tcm_rbd_configure_device(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct request_queue *q;
	struct block_device *bd = NULL;
	fmode_t mode;

	if (!(tcm_rbd_dev->bd_flags & TCM_RBD_HAS_UDEV_PATH)) {
		pr_err("Missing udev_path= parameters for TCM RBD\n");
		return -EINVAL;
	}

	pr_debug( "TCM RBD: Claiming struct block_device: %s\n",
		 tcm_rbd_dev->bd_udev_path);

	mode = FMODE_READ|FMODE_EXCL;
	if (!tcm_rbd_dev->bd_readonly)
		mode |= FMODE_WRITE;

	bd = blkdev_get_by_path(tcm_rbd_dev->bd_udev_path, mode, tcm_rbd_dev);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	tcm_rbd_dev->bd = bd;

	q = bdev_get_queue(bd);
	tcm_rbd_dev->rbd_dev = q->queuedata;

	dev->dev_attrib.hw_block_size = bdev_logical_block_size(bd);
	dev->dev_attrib.hw_max_sectors = queue_max_hw_sectors(q);
	dev->dev_attrib.hw_queue_depth = q->nr_requests;

	if (target_configure_unmap_from_queue(&dev->dev_attrib, q))
		pr_debug("RBD: BLOCK Discard support available,"
			 " disabled by default\n");

	/*
	 * Enable write same emulation for RBD and use 0xFFFF as
	 * the smaller WRITE_SAME(10) only has a two-byte block count.
	 */
	dev->dev_attrib.max_write_same_len = 0xFFFF;
	dev->dev_attrib.is_nonrot = 1;
	return 0;
}

static void tcm_rbd_free_device(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);

	if (tcm_rbd_dev->bd != NULL)
		blkdev_put(tcm_rbd_dev->bd, FMODE_WRITE|FMODE_READ|FMODE_EXCL);

	kfree(tcm_rbd_dev);
}

static sector_t tcm_rbd_get_blocks(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	sector_t blocks_long = tcm_rbd_dev->rbd_dev->mapping.size >>
								SECTOR_SHIFT;

	if (SECTOR_SIZE == dev->dev_attrib.block_size)
		return blocks_long;

	switch (SECTOR_SIZE) {
	case 4096:
		switch (dev->dev_attrib.block_size) {
		case 2048:
			blocks_long <<= 1;
			break;
		case 1024:
			blocks_long <<= 2;
			break;
		case 512:
			blocks_long <<= 3;
		default:
			break;
		}
		break;
	case 2048:
		switch (dev->dev_attrib.block_size) {
		case 4096:
			blocks_long >>= 1;
			break;
		case 1024:
			blocks_long <<= 1;
			break;
		case 512:
			blocks_long <<= 2;
			break;
		default:
			break;
		}
		break;
	case 1024:
		switch (dev->dev_attrib.block_size) {
		case 4096:
			blocks_long >>= 2;
			break;
		case 2048:
			blocks_long >>= 1;
			break;
		case 512:
			blocks_long <<= 1;
			break;
		default:
			break;
		}
		break;
	case 512:
		switch (dev->dev_attrib.block_size) {
		case 4096:
			blocks_long >>= 3;
			break;
		case 2048:
			blocks_long >>= 2;
			break;
		case 1024:
			blocks_long >>= 1;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return blocks_long;
}

static void rbd_complete_cmd(struct se_cmd *cmd)
{
	struct rbd_img_request *img_request = cmd->priv;
	u8 status = SAM_STAT_GOOD;

	if (img_request && img_request->result)
		status = SAM_STAT_CHECK_CONDITION;
	else
		status = SAM_STAT_GOOD;

	target_complete_cmd(cmd, status);
	if (img_request)
		rbd_img_request_put(img_request);
}

static sense_reason_t tcm_rbd_execute_sync_cache(struct se_cmd *cmd)
{
	/* Ceph/Rados supports flush, but kRBD does not yet */
	target_complete_cmd(cmd, SAM_STAT_GOOD);
	return 0;
}

/*
 * Convert the blocksize advertised to the initiator to the RBD offset.
 */
static u64 rbd_lba_shift(struct se_device *dev, unsigned long long task_lba)
{
	sector_t block_lba;

	/* convert to linux block which uses 512 byte sectors */
	if (dev->dev_attrib.block_size == 4096)
		block_lba = task_lba << 3;
	else if (dev->dev_attrib.block_size == 2048)
		block_lba = task_lba << 2;
	else if (dev->dev_attrib.block_size == 1024)
		block_lba = task_lba << 1;
	else
		block_lba = task_lba;

	/* convert to RBD offset */
	return block_lba << SECTOR_SHIFT;
}

static void tcm_rbd_async_callback(struct rbd_img_request *img_request)
{
	rbd_complete_cmd(img_request->lio_cmd_data);
}

static void tcm_rbd_sync_callback(struct rbd_img_request *img_request)
{
	struct completion *waiting = img_request->lio_cmd_data;

	complete(waiting);
}

static sense_reason_t
tcm_rbd_execute_cmd(struct se_cmd *cmd, struct rbd_device *rbd_dev,
		    struct scatterlist *sgl, enum obj_operation_type op_type,
		    u64 offset, u64 length, bool sync)
{
	struct rbd_img_request *img_request;
	struct ceph_snap_context *snapc = NULL;
	DECLARE_COMPLETION_ONSTACK(wait);
	sense_reason_t sense = TCM_NO_SENSE;
	int ret;

	if (op_type == OBJ_OP_WRITE || op_type == OBJ_OP_WRITESAME) {
		down_read(&rbd_dev->header_rwsem);
		snapc = rbd_dev->header.snapc;
		ceph_get_snap_context(snapc);
		up_read(&rbd_dev->header_rwsem);
	}

	img_request = rbd_img_request_create(rbd_dev, offset, length,
					     op_type, snapc);
	if (!img_request) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_snapc;
	}
	snapc = NULL; /* img_request consumes a ref */

	ret = rbd_img_request_fill(img_request,
				   sgl ? OBJ_REQUEST_SG : OBJ_REQUEST_NODATA,
				   sgl);
	if (ret) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_req;
	}

	if (sync) {
		img_request->lio_cmd_data = &wait;
		img_request->callback = tcm_rbd_sync_callback;
	} else {
		img_request->lio_cmd_data = cmd;
		img_request->callback = tcm_rbd_async_callback;
	}
	cmd->priv = img_request;

	ret = rbd_img_request_submit(img_request);
	if (ret == -ENOMEM) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_req;
	} else if (ret) {
		sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto free_req;
	}

	if (sync) {
		wait_for_completion(&wait);
		if (img_request->result)
			sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		rbd_img_request_put(img_request);
	}

	return sense;

free_req:
	rbd_img_request_put(img_request);
free_snapc:
	ceph_put_snap_context(snapc);
	return sense;
}

static sense_reason_t tcm_rbd_execute_unmap(struct se_cmd *cmd,
					    sector_t lba, sector_t nolb)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(cmd->se_dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;

	if (nolb == 0) {
		pr_debug("ignoring zero length unmap at lba: %llu\n",
			 (unsigned long long)lba);
		return TCM_NO_SENSE;
	}

	return tcm_rbd_execute_cmd(cmd, rbd_dev, NULL, OBJ_OP_DISCARD,
				   lba << SECTOR_SHIFT, nolb << SECTOR_SHIFT,
				   true);
}

static sense_reason_t tcm_rbd_execute_write_same(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	sector_t sectors = sbc_get_write_same_sectors(cmd);
	u64 length = rbd_lba_shift(dev, sectors);
	struct scatterlist *sg;

	if (cmd->prot_op) {
		pr_err("WRITE_SAME: Protection information with IBLOCK"
		       " backends not supported\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	sg = &cmd->t_data_sg[0];

	if (cmd->t_data_nents > 1 ||
	    sg->length != cmd->se_dev->dev_attrib.block_size) {
		pr_err("WRITE_SAME: Illegal SGL t_data_nents: %u length: %u"
			" block_size: %u\n", cmd->t_data_nents, sg->length,
			cmd->se_dev->dev_attrib.block_size);
		return TCM_INVALID_CDB_FIELD;
	}

	return tcm_rbd_execute_cmd(cmd, rbd_dev, sg, OBJ_OP_WRITESAME,
				   rbd_lba_shift(dev, cmd->t_task_lba), length,
				   false);
}

struct tcm_rbd_caw_state {
	struct se_cmd *cmd;
	struct scatterlist *cmp_and_write_sg;
};

static void tcm_rbd_cmp_and_write_callback(struct rbd_img_request *img_request)
{
	struct tcm_rbd_caw_state *caw_state = img_request->lio_cmd_data;
	struct se_cmd *cmd = caw_state->cmd;
	sense_reason_t sense_reason = TCM_NO_SENSE;

	if (img_request->result <= -MAX_ERRNO) {
		/* OSDs return -MAX_ERRNO - offset_of_mismatch */
		cmd->sense_info = (u32)(-1 * (img_request->result + MAX_ERRNO));
		pr_notice("COMPARE_AND_WRITE: miscompare at offset %llu\n",
			  (unsigned long long)cmd->bad_sector);
		sense_reason = TCM_MISCOMPARE_VERIFY;
	}
	kfree(caw_state->cmp_and_write_sg);
	kfree(caw_state);

	if (sense_reason != TCM_NO_SENSE) {
		/* TODO pass miscompare offset */
		target_complete_cmd_with_sense(cmd, sense_reason);
	} else if (img_request->result) {
		target_complete_cmd(cmd, SAM_STAT_CHECK_CONDITION);
	} else {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
	}
	rbd_img_request_put(img_request);
}

static sense_reason_t tcm_rbd_execute_cmp_and_write(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	struct tcm_rbd_caw_state *caw_state = NULL;
	struct rbd_img_request *img_request;
	struct ceph_snap_context *snapc;
	sense_reason_t sense = TCM_NO_SENSE;
	unsigned int len = cmd->t_task_nolb * dev->dev_attrib.block_size;
	int ret;

	down_read(&rbd_dev->header_rwsem);
	snapc = rbd_dev->header.snapc;
	ceph_get_snap_context(snapc);
	up_read(&rbd_dev->header_rwsem);

	/*
	 * No need to take dev->caw_sem here, as the IO is mapped to a compound
	 * compare+write OSD request, which is handled atomically by the OSD.
	 */

	img_request = rbd_img_request_create(rbd_dev,
					     rbd_lba_shift(dev, cmd->t_task_lba),
					     len, OBJ_OP_CMP_AND_WRITE, snapc);
	if (!img_request) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_snapc;
	}
	snapc = NULL; /* img_request consumes a ref */

	caw_state = kmalloc(sizeof(*caw_state), GFP_KERNEL);
	if (caw_state == NULL) {
		sense = TCM_OUT_OF_RESOURCES;
 		goto free_req;
 	}

	caw_state->cmp_and_write_sg = sbc_create_compare_and_write_sg(cmd);
	if (!caw_state->cmp_and_write_sg) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_caw_state;
	}

	ret = rbd_img_cmp_and_write_request_fill(img_request, cmd->t_data_sg,
						 len,
						 caw_state->cmp_and_write_sg,
						 len);
	if (ret == -EOPNOTSUPP) {
		sense = TCM_INVALID_CDB_FIELD;
		goto free_write_sg;
	} else if (ret) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_write_sg;
	}

	cmd->priv = img_request;
	caw_state->cmd = cmd;
	img_request->lio_cmd_data = caw_state;
	img_request->callback = tcm_rbd_cmp_and_write_callback;

	ret = rbd_img_request_submit(img_request);

	if (ret == -ENOMEM) {
		sense = TCM_OUT_OF_RESOURCES;
		goto free_write_sg;
	} else if (ret) {
		sense = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto free_write_sg;
	}
	return 0;

free_write_sg:
	kfree(caw_state->cmp_and_write_sg);
	caw_state->cmp_and_write_sg = NULL;
free_caw_state:
	kfree(caw_state);
free_req:
	rbd_img_request_put(img_request);
free_snapc:
	ceph_put_snap_context(snapc);
	return sense;
}

enum {
	Opt_udev_path, Opt_readonly, Opt_force, Opt_err
};

static match_table_t tokens = {
	{Opt_udev_path, "udev_path=%s"},
	{Opt_readonly, "readonly=%d"},
	{Opt_force, "force=%d"},
	{Opt_err, NULL}
};

static ssize_t
tcm_rbd_set_configfs_dev_params(struct se_device *dev, const char *page,
				ssize_t count)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char *orig, *ptr, *arg_p, *opts;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0, token;
	unsigned long tmp_readonly;

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;

	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_udev_path:
			if (tcm_rbd_dev->bd) {
				pr_err("Unable to set udev_path= while"
					" tcm_rbd_dev->bd exists\n");
				ret = -EEXIST;
				goto out;
			}
			if (match_strlcpy(tcm_rbd_dev->bd_udev_path, &args[0],
				SE_UDEV_PATH_LEN) == 0) {
				ret = -EINVAL;
				break;
			}
			pr_debug("TCM RBD: Referencing UDEV path: %s\n",
				 tcm_rbd_dev->bd_udev_path);
			tcm_rbd_dev->bd_flags |= TCM_RBD_HAS_UDEV_PATH;
			break;
		case Opt_readonly:
			arg_p = match_strdup(&args[0]);
			if (!arg_p) {
				ret = -ENOMEM;
				break;
			}
			ret = kstrtoul(arg_p, 0, &tmp_readonly);
			kfree(arg_p);
			if (ret < 0) {
				pr_err("kstrtoul() failed for readonly=\n");
				goto out;
			}
			tcm_rbd_dev->bd_readonly = tmp_readonly;
			pr_debug("TCM RBD: readonly: %d\n",
				 tcm_rbd_dev->bd_readonly);
			break;
		case Opt_force:
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return (!ret) ? count : ret;
}

static ssize_t tcm_rbd_show_configfs_dev_params(struct se_device *dev, char *b)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;
	char buf[BDEVNAME_SIZE];
	ssize_t bl = 0;

	if (bd)
		bl += sprintf(b + bl, "rbd device: %s", bdevname(bd, buf));
	if (tcm_rbd_dev->bd_flags & TCM_RBD_HAS_UDEV_PATH)
		bl += sprintf(b + bl, "  UDEV PATH: %s",
			      tcm_rbd_dev->bd_udev_path);
	bl += sprintf(b + bl, "  readonly: %d\n", tcm_rbd_dev->bd_readonly);

	bl += sprintf(b + bl, "        ");
	if (bd) {
		bl += sprintf(b + bl, "Major: %d Minor: %d  %s\n",
			      MAJOR(bd->bd_dev), MINOR(bd->bd_dev),
			      (!bd->bd_contains) ?
			      "" : (bd->bd_holder == tcm_rbd_dev) ?
			      "CLAIMED: RBD" : "CLAIMED: OS");
	} else {
		bl += sprintf(b + bl, "Major: 0 Minor: 0\n");
	}

	return bl;
}

static sense_reason_t
tcm_rbd_execute_rw(struct se_cmd *cmd, struct scatterlist *sgl, u32 sgl_nents,
		   enum dma_data_direction data_direction)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct rbd_device *rbd_dev = tcm_rbd_dev->rbd_dev;
	enum obj_operation_type op_type;

	if (!sgl_nents) {
		rbd_complete_cmd(cmd);
		return 0;
	}

	if (data_direction == DMA_FROM_DEVICE) {
		op_type = OBJ_OP_READ;
	} else {
		op_type = OBJ_OP_WRITE;
	}

	return tcm_rbd_execute_cmd(cmd, rbd_dev, sgl, op_type,
				   rbd_lba_shift(dev, cmd->t_task_lba),
				   cmd->data_length, false);
}

static sector_t tcm_rbd_get_alignment_offset_lbas(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;
	int ret;

	ret = bdev_alignment_offset(bd);
	if (ret == -1)
		return 0;

	/* convert offset-bytes to offset-lbas */
	return ret / bdev_logical_block_size(bd);
}

static unsigned int tcm_rbd_get_lbppbe(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;
	int logs_per_phys = bdev_physical_block_size(bd) / bdev_logical_block_size(bd);

	return ilog2(logs_per_phys);
}

static unsigned int tcm_rbd_get_io_min(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;

	return bdev_io_min(bd);
}

static unsigned int tcm_rbd_get_io_opt(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct block_device *bd = tcm_rbd_dev->bd;

	return bdev_io_opt(bd);
}

static struct sbc_ops tcm_rbd_sbc_ops = {
	.execute_rw		= tcm_rbd_execute_rw,
	.execute_sync_cache	= tcm_rbd_execute_sync_cache,
	.execute_write_same	= tcm_rbd_execute_write_same,
	.execute_unmap		= tcm_rbd_execute_unmap,
	.execute_compare_and_write = tcm_rbd_execute_cmp_and_write,
};

static sense_reason_t tcm_rbd_parse_cdb(struct se_cmd *cmd)
{
	return sbc_parse_cdb(cmd, &tcm_rbd_sbc_ops);
}

static bool tcm_rbd_get_write_cache(struct se_device *dev)
{
	return false;
}

#define TCM_RBD_PR_INFO_XATTR_KEY "pr_info"
#define TCM_RBD_PR_INFO_XATTR_VERS 1

#define TCM_RBD_PR_INFO_XATTR_FIELD_VER		0
#define TCM_RBD_PR_INFO_XATTR_FIELD_SEQ		1
#define TCM_RBD_PR_INFO_XATTR_FIELD_SCSI2_RSV	2
#define TCM_RBD_PR_INFO_XATTR_FIELD_GEN		3
#define TCM_RBD_PR_INFO_XATTR_FIELD_SCSI3_RSV	4
#define TCM_RBD_PR_INFO_XATTR_FIELD_NUM_REGS	5
#define TCM_RBD_PR_INFO_XATTR_FIELD_REGS_START	6

#define TCM_RBD_PR_INFO_XATTR_VAL_SCSI3_RSV_ABSENT	"No SPC-3 Reservation holder"
#define TCM_RBD_PR_INFO_XATTR_VAL_SCSI2_RSV_ABSENT	"No SPC-2 Reservation holder"

/* don't allow encoded PR info to exceed 8K */
#define TCM_RBD_PR_INFO_XATTR_MAX_SIZE 8192

/*
 * TRANSPORT_IQN_LEN + strlen(",i,0x") + sizeof(u64) * 2 + strlen(",")
 *	+ TRANSPORT_IQN_LEN + strlen(",t,0x") + sizeof(u32) * 2 + sizeof("\0") =
 */
#define TCM_RBD_PR_IT_NEXUS_MAXLEN	484

/* number of retries amid concurrent PR info changes from other nodes */
#define TCM_RBD_PR_REG_MAX_RETRIES	5

/*
 * Persistent reservation info. This structure is converted to and from a
 * string for storage within an RBD object xattr. String based storage allows
 * us to use xattr compare and write operations for atomic PR info updates.
 */
struct tcm_rbd_pr_rsv {
	u64 key;		/* registered key */
	/*
	 * I-T nexus for reservation. Separate to reg, so that all_tg_pt flag
	 * can be supported in future.
	 */
	char it_nexus[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	int type;		/* PR_TYPE_... */
	/* scope is always PR_SCOPE_LU_SCOPE */
};
#define TCM_RBD_PR_INFO_XATTR_ENCODED_PR_RSV_MAXLEN		\
	((sizeof("0x") + sizeof(u64) * 2) + sizeof(" ") +	\
	 TCM_RBD_PR_IT_NEXUS_MAXLEN + sizeof(" ") +		\
	 (sizeof("0x") + sizeof(u64) * 2) + sizeof("\n"))

struct tcm_rbd_pr_reg {
	struct list_head regs_node;
	u64 key;		/* registered key */
	/* I-T nexus for registration */
	char it_nexus[TCM_RBD_PR_IT_NEXUS_MAXLEN];
};
#define TCM_RBD_PR_INFO_XATTR_ENCODED_PR_REG_MAXLEN		\
	((sizeof("0x") + sizeof(u64) * 2) + sizeof(" ") +	\
	 TCM_RBD_PR_IT_NEXUS_MAXLEN + sizeof("\n"))

struct tcm_rbd_scsi2_rsv {
	/*
	 * I-T nexus for SCSI2 (RESERVE/RELEASE) reservation.
	 */
	char it_nexus[TCM_RBD_PR_IT_NEXUS_MAXLEN];
};
#define TCM_RBD_PR_INFO_XATTR_ENCODED_SCSI2_RSV_MAXLEN		\
	(TCM_RBD_PR_IT_NEXUS_MAXLEN + sizeof("\n"))

struct tcm_rbd_pr_info {
	u32 vers;		/* on disk format version number */
	u32 seq; 		/* sequence number bumped every xattr write */
	struct tcm_rbd_scsi2_rsv *scsi2_rsv; /* SCSI2 reservation if any */
	u32 gen; 		/* PR generation number */
	struct tcm_rbd_pr_rsv *rsv;	/* SCSI3 reservation if any */
	u32 num_regs;		/* number of registrations */
	struct list_head regs;	/* list of registrations */
};
#define TCM_RBD_PR_INFO_XATTR_ENCODED_MAXLEN(_num_regs)			\
	((sizeof("0x") + sizeof(u32) * 2) + sizeof("\n") +		\
	 (sizeof("0x") + sizeof(u32) * 2) + sizeof("\n") +		\
	 TCM_RBD_PR_INFO_XATTR_ENCODED_SCSI2_RSV_MAXLEN + 		\
	 (sizeof("0x") + sizeof(u32) * 2) + sizeof("\n") +		\
	 TCM_RBD_PR_INFO_XATTR_ENCODED_PR_RSV_MAXLEN +	 		\
	 (sizeof("0x") + sizeof(u32) * 2) + sizeof("\n") +		\
	 (TCM_RBD_PR_INFO_XATTR_ENCODED_PR_REG_MAXLEN * _num_regs) +	\
	 sizeof("\0"))

static int
tcm_rbd_gen_it_nexus(struct se_session *se_sess,
		     char *nexus_buf,
		     size_t buflen)
{
	struct se_portal_group *se_tpg;
	const struct target_core_fabric_ops *tfo;
	u32 tpg_tag = 0;
	char *tpg_wwn = "";
	int rc;

	if (!se_sess || !se_sess->se_node_acl || !se_sess->se_tpg
					|| !se_sess->se_tpg->se_tpg_tfo) {
		pr_warn("invalid session for IT nexus generation\n");
		return -EINVAL;
	}

	se_tpg = se_sess->se_tpg;
	tfo = se_tpg->se_tpg_tfo;

	/*
	 * nexus generation may be coming from an xcopy, in which case tfo
	 * refers to xcopy_pt_tfo (tpg_get_wwn and tpg_get_tag are NULL).
	 */
	if (tfo->tpg_get_tag) {
		tpg_tag = tfo->tpg_get_tag(se_tpg);
	}
	if (tfo->tpg_get_wwn) {
		tpg_wwn = tfo->tpg_get_wwn(se_tpg);
	}

	rc = snprintf(nexus_buf, buflen, "%s,i,0x%llx,%s,t,0x%x",
		      se_sess->se_node_acl->initiatorname,
		      se_sess->sess_bin_isid,
		      tpg_wwn,
		      tpg_tag);
	if ((rc < 0) || (rc >= buflen)) {
		pr_err("error formatting reserve cookie\n");
		return -EINVAL;
	}

	pr_debug("generated nexus: %s\n", nexus_buf);

	return 0;
}

static void
tcm_rbd_pr_info_free(struct tcm_rbd_pr_info *pr_info)
{
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *reg_n;

	kfree(pr_info->scsi2_rsv);
	kfree(pr_info->rsv);
	list_for_each_entry_safe(reg, reg_n, &pr_info->regs, regs_node) {
		kfree(reg);
	}
	kfree(pr_info);
}

static bool
tcm_rbd_is_rsv_holder(struct tcm_rbd_pr_rsv *rsv, struct tcm_rbd_pr_reg *reg,
		      bool *rsv_is_all_reg)
{
	BUG_ON(!rsv);
	BUG_ON(!reg);
	if ((rsv->type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG)
			|| (rsv->type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)) {
		/* any registeration is a reservation holder */
		if (rsv_is_all_reg)
			*rsv_is_all_reg = true;
		return true;
	}
	if (rsv_is_all_reg)
		*rsv_is_all_reg = false;

	if ((rsv->key == reg->key)
	 && !strncmp(rsv->it_nexus, reg->it_nexus, ARRAY_SIZE(rsv->it_nexus))) {
		return true;
	}

	return false;
}

static int
tcm_rbd_pr_info_rsv_set(struct tcm_rbd_pr_info *pr_info, u64 key, char *nexus,
			int type)
{
	struct tcm_rbd_pr_rsv *rsv;

	if (pr_info->rsv != NULL) {
		pr_err("rsv_set called with existing reservation\n");
		return -EINVAL;
	}

	rsv = kmalloc(sizeof(*rsv), GFP_KERNEL);
	if (!rsv) {
		return -ENOMEM;
	}

	rsv->key = key;
	strlcpy(rsv->it_nexus, nexus, ARRAY_SIZE(rsv->it_nexus));
	rsv->type = type;

	pr_info->rsv = rsv;

	dout("pr_info rsv set: 0x%llx %s %d\n", key, nexus, type);

	return 0;
}

static void
tcm_rbd_pr_info_rsv_clear(struct tcm_rbd_pr_info *pr_info)
{
	kfree(pr_info->rsv);
	pr_info->rsv = NULL;

	dout("pr_info rsv cleared\n");
}

static int
tcm_rbd_pr_info_append_reg(struct tcm_rbd_pr_info *pr_info, char *nexus,
			   u64 key)
{
	struct tcm_rbd_pr_reg *reg;

	reg = kmalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg) {
		return -ENOMEM;
	}

	reg->key = key;
	strlcpy(reg->it_nexus, nexus, ARRAY_SIZE(reg->it_nexus));

	list_add_tail(&reg->regs_node, &pr_info->regs);
	pr_info->num_regs++;

	dout("appended pr_info reg: 0x%llx\n", reg->key);

	return 0;
}

static void
tcm_rbd_pr_info_clear_reg(struct tcm_rbd_pr_info *pr_info,
			  struct tcm_rbd_pr_reg *reg)
{
	list_del(&reg->regs_node);
	pr_info->num_regs--;

	dout("deleted pr_info reg: 0x%llx\n", reg->key);

	kfree(reg);
}

static int
tcm_rbd_pr_info_unregister_reg(struct tcm_rbd_pr_info *pr_info,
			       struct tcm_rbd_pr_reg *reg)
{
	struct tcm_rbd_pr_rsv *rsv;
	bool all_reg = false;

	rsv = pr_info->rsv;
	if (rsv && tcm_rbd_is_rsv_holder(rsv, reg, &all_reg)) {
		/*
		 * If the persistent reservation holder is more than one I_T
		 * nexus, the reservation shall not be released until the
		 * registrations for all persistent reservation holder I_T
		 * nexuses are removed.
		 */
		if (!all_reg || (pr_info->num_regs == 1)) {
			pr_warn("implicitly releasing PR of type %d on "
				"unregister from %s\n",
				rsv->type, reg->it_nexus);
			tcm_rbd_pr_info_rsv_clear(pr_info);
		}
	}

	tcm_rbd_pr_info_clear_reg(pr_info, reg);

	return 0;
}

static int
tcm_rbd_pr_info_scsi2_rsv_set(struct tcm_rbd_pr_info *pr_info, char *nexus)
{
	struct tcm_rbd_scsi2_rsv *scsi2_rsv;

	if (pr_info->scsi2_rsv != NULL) {
		pr_err("rsv_set called with existing SCSI2 reservation\n");
		return -EINVAL;
	}

	scsi2_rsv = kmalloc(sizeof(*scsi2_rsv), GFP_KERNEL);
	if (!scsi2_rsv) {
		return -ENOMEM;
	}

	strlcpy(scsi2_rsv->it_nexus, nexus, ARRAY_SIZE(scsi2_rsv->it_nexus));

	pr_info->scsi2_rsv = scsi2_rsv;

	dout("pr_info scsi2_rsv set: %s\n", nexus);

	return 0;
}

static void
tcm_rbd_pr_info_scsi2_rsv_clear(struct tcm_rbd_pr_info *pr_info)
{
	dout("pr_info scsi2_rsv clearing: %s\n", pr_info->scsi2_rsv->it_nexus);
	kfree(pr_info->scsi2_rsv);
	pr_info->scsi2_rsv = NULL;
}

static int
tcm_rbd_pr_info_vers_decode(char *str, u32 *vers)
{
	int rc;

	BUG_ON(!vers);
	rc = sscanf(str, "0x%08x", vers);
	if (rc != 1) {
		pr_err("failed to decode PR info version in: %s\n", str);
		return -EINVAL;
	}

	if (*vers != TCM_RBD_PR_INFO_XATTR_VERS) {
		pr_err("unsupported PR info version: %u\n", *vers);
		return -EINVAL;
	}

	dout("processed pr_info version: %u\n", *vers);
	return 0;
}

static int
tcm_rbd_pr_info_seq_decode(char *str, u32 *seq)
{
	int rc;

	BUG_ON(!seq);
	rc = sscanf(str, "0x%08x", seq);
	if (rc != 1) {
		pr_err("failed to decode PR info seqnum in: %s\n", str);
		return -EINVAL;
	}

	dout("processed pr_info seqnum: %u\n", *seq);
	return 0;
}

static int
tcm_rbd_pr_info_scsi2_rsv_decode(char *str,
				 struct tcm_rbd_scsi2_rsv **_scsi2_rsv)
{
	struct tcm_rbd_scsi2_rsv *scsi2_rsv;

	BUG_ON(!_scsi2_rsv);
	if (!strncmp(str, TCM_RBD_PR_INFO_XATTR_VAL_SCSI2_RSV_ABSENT,
		     sizeof(TCM_RBD_PR_INFO_XATTR_VAL_SCSI2_RSV_ABSENT))) {
		scsi2_rsv = NULL;
	} else {
		size_t n;

		scsi2_rsv = kzalloc(sizeof(*scsi2_rsv), GFP_KERNEL);
		if (!scsi2_rsv) {
			return -ENOMEM;
		}

		n = strlcpy(scsi2_rsv->it_nexus, str,
			    TCM_RBD_PR_IT_NEXUS_MAXLEN);
		if (n >= TCM_RBD_PR_IT_NEXUS_MAXLEN) {
			kfree(scsi2_rsv);
			return -EINVAL;
		}
	}

	dout("processed pr_info SCSI2 rsv: %s\n", str);
	*_scsi2_rsv = scsi2_rsv;
	return 0;
}

static int
tcm_rbd_pr_info_gen_decode(char *str, u32 *gen)
{
	int rc;

	BUG_ON(!gen);
	rc = sscanf(str, "0x%08x", gen);
	if (rc != 1) {
		pr_err("failed to parse PR gen: %s\n", str);
		return -EINVAL;
	}
	dout("processed pr_info generation: %s\n", str);
	return 0;
}

static int
tcm_rbd_pr_info_num_regs_decode(char *str, u32 *num_regs)
{
	int rc;

	BUG_ON(!num_regs);
	rc = sscanf(str, "0x%08x", num_regs);
	if (rc != 1) {
		pr_err("failed to parse PR num regs: %s\n", str);
		return -EINVAL;
	}
	dout("processed pr_info num_regs: %s\n", str);
	return 0;
}

static int
tcm_rbd_pr_info_rsv_decode(char *str, struct tcm_rbd_pr_rsv **_rsv)
{
	struct tcm_rbd_pr_rsv *rsv;
	int rc;

	BUG_ON(!_rsv);
	if (!strncmp(str, TCM_RBD_PR_INFO_XATTR_VAL_SCSI3_RSV_ABSENT,
		    sizeof(TCM_RBD_PR_INFO_XATTR_VAL_SCSI3_RSV_ABSENT))) {
		/* no reservation. Ensure pr_info->rsv is NULL */
		rsv = NULL;
	} else {
		rsv = kzalloc(sizeof(*rsv), GFP_KERNEL);
		if (!rsv) {
			return -ENOMEM;
		}

		/* reservation key, I-T nexus, and type with space separators */
		rc = sscanf(str, "0x%016llx %"
			    __stringify(TCM_RBD_PR_IT_NEXUS_MAXLEN)
			    "s 0x%08x", &rsv->key, rsv->it_nexus, &rsv->type);
		if (rc != 3) {
			pr_err("failed to parse PR rsv: %s\n", str);
			kfree(rsv);
			return -EINVAL;
		}
	}

	dout("processed pr_info rsv: %s\n", str);
	*_rsv = rsv;
	return 0;
}

static int
tcm_rbd_pr_info_reg_decode(char *str, struct tcm_rbd_pr_reg **_reg)
{
	struct tcm_rbd_pr_reg *reg;
	int rc;

	BUG_ON(!_reg);
	reg = kzalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg) {
		return -ENOMEM;
	}

	/* registration key and I-T nexus with space separator */
	rc = sscanf(str, "0x%016llx %" __stringify(TCM_RBD_PR_IT_NEXUS_MAXLEN)
		    "s", &reg->key, reg->it_nexus);
	if (rc != 2) {
		pr_err("failed to parse PR reg: %s\n", str);
		kfree(reg);
		return -EINVAL;
	}

	dout("processed pr_info reg: %s\n", str);
	*_reg = reg;
	return 0;
}

static int
tcm_rbd_pr_info_decode(char *pr_xattr,
		       int pr_xattr_len,
		       struct tcm_rbd_pr_info **_pr_info)
{
	struct tcm_rbd_pr_info *pr_info;
	int rc;
	int field;
	int i;
	char *p;
	char *str;
	char *end;

	BUG_ON(!_pr_info);

	if (!pr_xattr_len) {
		pr_err("zero length PR xattr\n");
		return -EINVAL;
	}

	dout("decoding PR xattr: %s\n", pr_xattr);

	pr_info = kzalloc(sizeof(*pr_info), GFP_KERNEL);
	if (!pr_info) {
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&pr_info->regs);

	p = pr_xattr;
	end = pr_xattr + pr_xattr_len;
	field = 0;
	i = 0;
	/*
	 * '\n' separator between header fields and each reg entry.
	 * reg subfields are further separated by ' '.
	 */
	for (str = strsep(&p, "\n"); str && *str != '\0' && (p < end);
						str = strsep(&p, "\n")) {

		if (field == TCM_RBD_PR_INFO_XATTR_FIELD_VER) {
			rc = tcm_rbd_pr_info_vers_decode(str, &pr_info->vers);
			if (rc < 0) {
				goto err_info_free;
			}
		} else if (field == TCM_RBD_PR_INFO_XATTR_FIELD_SEQ) {
			rc = tcm_rbd_pr_info_seq_decode(str, &pr_info->seq);
			if (rc < 0) {
				goto err_info_free;
			}
		} else if (field == TCM_RBD_PR_INFO_XATTR_FIELD_SCSI2_RSV) {
			rc = tcm_rbd_pr_info_scsi2_rsv_decode(str,
							&pr_info->scsi2_rsv);
			if (rc < 0) {
				goto err_info_free;
			}
		} else if (field == TCM_RBD_PR_INFO_XATTR_FIELD_GEN) {
			rc = tcm_rbd_pr_info_gen_decode(str, &pr_info->gen);
			if (rc < 0) {
				goto err_info_free;
			}
		} else if (field == TCM_RBD_PR_INFO_XATTR_FIELD_SCSI3_RSV) {
			rc = tcm_rbd_pr_info_rsv_decode(str, &pr_info->rsv);
			if (rc < 0) {
				goto err_info_free;
			}
		} else if (field == TCM_RBD_PR_INFO_XATTR_FIELD_NUM_REGS) {
			rc = tcm_rbd_pr_info_num_regs_decode(str,
							    &pr_info->num_regs);
			if (rc < 0) {
				goto err_info_free;
			}
		} else if (field >= TCM_RBD_PR_INFO_XATTR_FIELD_REGS_START) {
			struct tcm_rbd_pr_reg *reg;
			rc = tcm_rbd_pr_info_reg_decode(str, &reg);
			if (rc < 0) {
				goto err_info_free;
			}
			list_add_tail(&reg->regs_node, &pr_info->regs);
			i++;
		} else {
			dout("skipping parsing of field %d\n", field);
		}

		field++;
	}

	if (field <= TCM_RBD_PR_INFO_XATTR_FIELD_NUM_REGS) {
		pr_err("pr_info missing basic fields, stopped at %d\n", field);
		rc = -EINVAL;
		goto err_info_free;
	}

	if (i != pr_info->num_regs) {
		pr_err("processed %d registrations, expected %d\n",
			 i, pr_info->num_regs);
		rc = -EINVAL;
		goto err_info_free;
	}

	dout("successfully processed all PR data\n");
	*_pr_info = pr_info;

	return 0;

err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	return rc;
}

static int
tcm_rbd_pr_info_vers_seq_encode(char *buf, size_t buf_remain, u32 vers, u32 seq)
{
	int rc;

	rc = snprintf(buf, buf_remain, "0x%08x\n0x%08x\n",
		      vers, seq);
	if ((rc < 0) || (rc >= buf_remain)) {
		pr_err("failed to encode PR vers and seq\n");
		return -EINVAL;
	}

	return rc;
}

static int
tcm_rbd_pr_info_scsi2_rsv_encode(char *buf, size_t buf_remain,
				 struct tcm_rbd_scsi2_rsv *scsi2_rsv)
{
	int rc;

	if (!scsi2_rsv) {
		/* no reservation */
		rc = snprintf(buf, buf_remain, "%s\n",
			      TCM_RBD_PR_INFO_XATTR_VAL_SCSI2_RSV_ABSENT);
	} else {
		rc = snprintf(buf, buf_remain, "%s\n", scsi2_rsv->it_nexus);
	}
	if ((rc < 0) || (rc >= buf_remain)) {
		pr_err("failed to encode SCSI2 reservation\n");
		return -EINVAL;
	}

	return rc;
}

static int
tcm_rbd_pr_info_gen_encode(char *buf, size_t buf_remain, u32 gen)
{
	int rc;

	rc = snprintf(buf, buf_remain, "0x%08x\n", gen);
	if ((rc < 0) || (rc >= buf_remain)) {
		pr_err("failed to encode PR gen\n");
		return -EINVAL;
	}

	return rc;
}

static int
tcm_rbd_pr_info_rsv_encode(char *buf, size_t buf_remain,
			   struct tcm_rbd_pr_rsv *rsv)
{
	int rc;

	if (!rsv) {
		/* no reservation */
		rc = snprintf(buf, buf_remain, "%s\n",
			      TCM_RBD_PR_INFO_XATTR_VAL_SCSI3_RSV_ABSENT);
	} else {
		rc = snprintf(buf, buf_remain, "0x%016llx %s 0x%08x\n",
			      rsv->key, rsv->it_nexus, rsv->type);
	}
	if ((rc < 0) || (rc >= buf_remain)) {
		pr_err("failed to encode PR reservation\n");
		return -EINVAL;
	}

	return rc;
}

static int
tcm_rbd_pr_info_num_regs_encode(char *buf, size_t buf_remain, u32 num_regs)
{
	int rc;

	rc = snprintf(buf, buf_remain, "0x%08x\n", num_regs);
	if ((rc < 0) || (rc >= buf_remain)) {
		pr_err("failed to encode PR num_regs\n");
		return -EINVAL;
	}

	return rc;
}

static int
tcm_rbd_pr_info_reg_encode(char *buf, size_t buf_remain,
			   struct tcm_rbd_pr_reg *reg)
{
	int rc;

	rc = snprintf(buf, buf_remain, "0x%016llx %s\n", reg->key, reg->it_nexus);
	if ((rc < 0) || (rc >= buf_remain)) {
		pr_err("failed to encode PR registration\n");
		return -EINVAL;
	}

	return rc;
}

static int
tcm_rbd_pr_info_encode(struct tcm_rbd_pr_info *pr_info,
		       char **_pr_xattr,
		       int *pr_xattr_len)
{
	struct tcm_rbd_pr_reg *reg;
	char *pr_xattr;
	char *p;
	size_t buf_remain;
	int rc;
	int i;

	if (pr_info->vers != TCM_RBD_PR_INFO_XATTR_VERS) {
		pr_err("unsupported PR info version: %u\n", pr_info->vers);
		return -EINVAL;
	}

	buf_remain = TCM_RBD_PR_INFO_XATTR_ENCODED_MAXLEN(pr_info->num_regs);
	if (buf_remain > TCM_RBD_PR_INFO_XATTR_MAX_SIZE) {
		pr_err("PR info too large for encoding: %zd\n", buf_remain);
		return -EINVAL;
	}

	dout("encoding PR info: vers=%u, seq=%u, gen=%u, num regs=%u into %zd "
	     "bytes\n", pr_info->vers, pr_info->seq, pr_info->gen,
	     pr_info->num_regs, buf_remain);

	pr_xattr = kmalloc(buf_remain, GFP_KERNEL);
	if (!pr_xattr) {
		return -ENOMEM;
	}

	p = pr_xattr;
	rc = tcm_rbd_pr_info_vers_seq_encode(p, buf_remain, pr_info->vers,
					     pr_info->seq);
	if (rc < 0) {
		rc = -EINVAL;
		goto err_xattr_free;
	}

	p += rc;
	buf_remain -= rc;

	rc = tcm_rbd_pr_info_scsi2_rsv_encode(p, buf_remain,
					      pr_info->scsi2_rsv);
	 if (rc < 0) {
		rc = -EINVAL;
		goto err_xattr_free;
	}

	p += rc;
	buf_remain -= rc;

	rc = tcm_rbd_pr_info_gen_encode(p, buf_remain, pr_info->gen);
	if (rc < 0) {
		rc = -EINVAL;
		goto err_xattr_free;
	}

	p += rc;
	buf_remain -= rc;

	rc = tcm_rbd_pr_info_rsv_encode(p, buf_remain, pr_info->rsv);
	 if (rc < 0) {
		rc = -EINVAL;
		goto err_xattr_free;
	}

	p += rc;
	buf_remain -= rc;

	rc = tcm_rbd_pr_info_num_regs_encode(p, buf_remain, pr_info->num_regs);
	if (rc < 0) {
		rc = -EINVAL;
		goto err_xattr_free;
	}

	p += rc;
	buf_remain -= rc;

	i = 0;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		rc = tcm_rbd_pr_info_reg_encode(p, buf_remain, reg);
		 if (rc < 0) {
			rc = -EINVAL;
			goto err_xattr_free;
		}

		p += rc;
		buf_remain -= rc;
		i++;
	}

	if (i != pr_info->num_regs) {
		pr_err("mismatch between PR num_regs and list entries!\n");
		rc = -EINVAL;
		goto err_xattr_free;
	}

	*_pr_xattr = pr_xattr;
	/* +1 to include null term */
	*pr_xattr_len = (p - pr_xattr) + 1;

	dout("successfully encoded all %d PR regs into %d bytes: %s\n",
	     pr_info->num_regs, *pr_xattr_len, pr_xattr);

	return 0;

err_xattr_free:
	kfree(pr_xattr);
	return rc;
}

static int
tcm_rbd_pr_info_mock_empty(struct tcm_rbd_dev *tcm_rbd_dev,
			   struct tcm_rbd_pr_info **_pr_info)
{
	struct tcm_rbd_pr_info *pr_info;

	pr_info = kzalloc(sizeof(*pr_info), GFP_KERNEL);
	if (!pr_info) {
		return -ENOMEM;
	}

	pr_info->vers = TCM_RBD_PR_INFO_XATTR_VERS;
	INIT_LIST_HEAD(&pr_info->regs);

	*_pr_info = pr_info;
	dout("successfully initialized mock PR info\n");

	return 0;
}

static int
tcm_rbd_pr_info_init(struct tcm_rbd_dev *tcm_rbd_dev,
		     struct tcm_rbd_pr_info **_pr_info,
		     char **_pr_xattr, int *_pr_xattr_len)

{
	struct tcm_rbd_pr_info *pr_info;
	char *pr_xattr = NULL;
	int pr_xattr_len = 0;
	int rc;

	pr_info = kzalloc(sizeof(*pr_info), GFP_KERNEL);
	if (!pr_info) {
		return -ENOMEM;
	}

	pr_info->vers = TCM_RBD_PR_INFO_XATTR_VERS;
	INIT_LIST_HEAD(&pr_info->regs);
	pr_info->seq = 1;

	rc = tcm_rbd_pr_info_encode(pr_info, &pr_xattr,
				    &pr_xattr_len);
	if (rc) {
		pr_warn("failed to encode PR xattr: %d\n", rc);
		goto err_info_free;
	}

	rc = rbd_dev_setxattr(tcm_rbd_dev->rbd_dev,
			      TCM_RBD_PR_INFO_XATTR_KEY,
			      pr_xattr, pr_xattr_len);
	if (rc) {
		pr_warn("failed to set PR xattr: %d\n", rc);
		goto err_xattr_free;
	}

	*_pr_info = pr_info;
	if (_pr_xattr) {
		BUG_ON(!_pr_xattr_len);
		*_pr_xattr = pr_xattr;
		*_pr_xattr_len = pr_xattr_len;
	} else {
		kfree(pr_xattr);
	}
	dout("successfully initialized PR info\n");

	return 0;

err_xattr_free:
	kfree(pr_xattr);
err_info_free:
	kfree(pr_info);
	return rc;
}

static int
tcm_rbd_pr_info_get(struct tcm_rbd_dev *tcm_rbd_dev,
		    struct tcm_rbd_pr_info **_pr_info,
		    char **_pr_xattr, int *_pr_xattr_len)
{
	int rc;
	char *pr_xattr = NULL;
	char *dup_xattr = NULL;
	int pr_xattr_len = 0;
	struct tcm_rbd_pr_info *pr_info = NULL;

	BUG_ON(!_pr_info);

	rc = rbd_dev_getxattr(tcm_rbd_dev->rbd_dev, TCM_RBD_PR_INFO_XATTR_KEY,
			      TCM_RBD_PR_INFO_XATTR_MAX_SIZE,
			      (void **)&pr_xattr, &pr_xattr_len);
	if (rc) {
		if (rc != -ENODATA)
			pr_warn("failed to obtain PR xattr: %d\n", rc);
		return rc;
	}
	if (_pr_xattr) {
		/* dup before decode, which trashes @pr_xattr */
		dup_xattr = kstrdup(pr_xattr, GFP_KERNEL);
		if (!dup_xattr) {
			rc = -ENOMEM;
			goto err_xattr_free;
		}
	}

	rc = tcm_rbd_pr_info_decode(pr_xattr, pr_xattr_len, &pr_info);
	if (rc) {
		pr_warn("failed to decode PR xattr: %d\n", rc);
		goto err_dup_xattr_free;
	}

	if (_pr_xattr) {
		BUG_ON(!_pr_xattr_len);
		*_pr_xattr = dup_xattr;
		*_pr_xattr_len = pr_xattr_len;
	}
	kfree(pr_xattr);
	*_pr_info = pr_info;
	dout("successfully obtained PR info\n");

	return 0;

err_dup_xattr_free:
	kfree(dup_xattr);
err_xattr_free:
	kfree(pr_xattr);
	return rc;
}

static int
tcm_rbd_pr_info_replace(struct tcm_rbd_dev *tcm_rbd_dev,
			char *pr_xattr_old, int pr_xattr_len_old,
			struct tcm_rbd_pr_info *pr_info_new)
{
	int rc;
	char *pr_xattr_new = NULL;
	int pr_xattr_len_new = 0;

	BUG_ON(!pr_xattr_old || !pr_info_new);

	/* bump seqnum prior to xattr write. Not rolled back on failure */
	pr_info_new->seq++;
	rc = tcm_rbd_pr_info_encode(pr_info_new, &pr_xattr_new,
				    &pr_xattr_len_new);
	if (rc) {
		pr_warn("failed to encode PR xattr: %d\n", rc);
		return rc;
	}

	if (pr_xattr_len_new > TCM_RBD_PR_INFO_XATTR_MAX_SIZE) {
		pr_err("unable to store oversize (%d) PR info: %s\n",
		       pr_xattr_len_new, pr_xattr_new);
		rc = -E2BIG;
		goto err_xattr_new_free;
	}

	rc = rbd_dev_cmpsetxattr(tcm_rbd_dev->rbd_dev,
				 TCM_RBD_PR_INFO_XATTR_KEY,
				 pr_xattr_old, pr_xattr_len_old,
				 pr_xattr_new, pr_xattr_len_new);
	if (rc) {
		pr_warn("failed to set PR xattr: %d\n", rc);
		goto err_xattr_new_free;
	}

	dout("successfully replaced PR info\n");
	rc = 0;
err_xattr_new_free:
	kfree(pr_xattr_new);

	return 0;
}

static sense_reason_t
tcm_rbd_execute_pr_read_keys(struct se_cmd *cmd, unsigned char *buf,
			     u32 buf_len)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct tcm_rbd_pr_info *pr_info = NULL;
	struct tcm_rbd_pr_reg *reg;
	u32 add_len = 0, off = 8;
	int rc;

	BUG_ON(buf_len < 8);

	dout("getting pr_info for buf: %p, %u\n", buf, buf_len);

	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, NULL, NULL);
	if (rc == -ENODATA) {
		dout("PR info not present for read, mocking empty\n");
		rc = tcm_rbd_pr_info_mock_empty(tcm_rbd_dev, &pr_info);
	}
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	dout("packing read_keys response buf: %p, %u\n", buf, buf_len);

	buf[0] = ((pr_info->gen >> 24) & 0xff);
	buf[1] = ((pr_info->gen >> 16) & 0xff);
	buf[2] = ((pr_info->gen >> 8) & 0xff);
	buf[3] = (pr_info->gen & 0xff);

	dout("packed gen %u in read_keys response\n", pr_info->gen);

	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		/*
		 * Check for overflow of 8byte PRI READ_KEYS payload and
		 * next reservation key list descriptor.
		 */
		if ((add_len + 8) > (buf_len - 8))
			break;

		buf[off++] = ((reg->key >> 56) & 0xff);
		buf[off++] = ((reg->key >> 48) & 0xff);
		buf[off++] = ((reg->key >> 40) & 0xff);
		buf[off++] = ((reg->key >> 32) & 0xff);
		buf[off++] = ((reg->key >> 24) & 0xff);
		buf[off++] = ((reg->key >> 16) & 0xff);
		buf[off++] = ((reg->key >> 8) & 0xff);
		buf[off++] = (reg->key & 0xff);
		dout("packed key 0x%llx in read_keys response\n", reg->key);

		add_len += 8;
	}

	buf[4] = ((add_len >> 24) & 0xff);
	buf[5] = ((add_len >> 16) & 0xff);
	buf[6] = ((add_len >> 8) & 0xff);
	buf[7] = (add_len & 0xff);
	dout("packed len %u in read_keys response\n", add_len);
	tcm_rbd_pr_info_free(pr_info);

	return TCM_NO_SENSE;
}

static sense_reason_t
tcm_rbd_execute_pr_read_reservation(struct se_cmd *cmd, unsigned char *buf,
				    u32 buf_len)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct tcm_rbd_pr_info *pr_info = NULL;
	u64 pr_res_key;
	u32 add_len = 16; /* Hardcoded to 16 when a reservation is held. */
	int rc;

	BUG_ON(buf_len < 8);

	dout("getting pr_info for buf: %p, %u\n", buf, buf_len);

	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, NULL, NULL);
	if (rc == -ENODATA) {
		dout("PR info not present for read, mocking empty\n");
		rc = tcm_rbd_pr_info_mock_empty(tcm_rbd_dev, &pr_info);
	}
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	buf[0] = ((pr_info->gen >> 24) & 0xff);
	buf[1] = ((pr_info->gen >> 16) & 0xff);
	buf[2] = ((pr_info->gen >> 8) & 0xff);
	buf[3] = (pr_info->gen & 0xff);

	if (pr_info->rsv) {
		buf[4] = ((add_len >> 24) & 0xff);
		buf[5] = ((add_len >> 16) & 0xff);
		buf[6] = ((add_len >> 8) & 0xff);
		buf[7] = (add_len & 0xff);

		if (buf_len < 22)
			goto out;

		if ((pr_info->rsv->type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG) ||
		    (pr_info->rsv->type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)) {
			/*
			 * a) For a persistent reservation of the type Write
			 *    Exclusive - All Registrants or Exclusive Access -
			 *    All Registrants, the reservation key shall be set
			 *    to zero; or
			 */
			pr_res_key = 0;
		} else {
			/*
			 * b) For all other persistent reservation types, the
			 *    reservation key shall be set to the registered
			 *    reservation key for the I_T nexus that holds the
			 *    persistent reservation.
			 */
			pr_res_key = pr_info->rsv->key;
		}

		buf[8] = ((pr_res_key >> 56) & 0xff);
		buf[9] = ((pr_res_key >> 48) & 0xff);
		buf[10] = ((pr_res_key >> 40) & 0xff);
		buf[11] = ((pr_res_key >> 32) & 0xff);
		buf[12] = ((pr_res_key >> 24) & 0xff);
		buf[13] = ((pr_res_key >> 16) & 0xff);
		buf[14] = ((pr_res_key >> 8) & 0xff);
		buf[15] = (pr_res_key & 0xff);
		/*
		 * Set the SCOPE and TYPE
		 */
		buf[21] = (PR_SCOPE_LU_SCOPE & 0xf0) |
			  (pr_info->rsv->type & 0x0f);
	}

out:
	tcm_rbd_pr_info_free(pr_info);
	return TCM_NO_SENSE;
}

static sense_reason_t
tcm_rbd_execute_pr_report_capabilities(struct se_cmd *cmd, unsigned char *buf,
				       u32 buf_len)
{
	u16 add_len = 8; /* Hardcoded to 8. */

	BUG_ON(buf_len < 6);

	buf[0] = ((add_len >> 8) & 0xff);
	buf[1] = (add_len & 0xff);
	buf[2] |= 0x10; /* CRH: Compatible Reservation Handling bit. */
	/* SIP_C=0 and ATP_C=0: no support for all_tg_pt/spec_i_pt */
	buf[2] |= 0x01; /* PTPL_C: Persistence across Target Power Loss bit */
	/*
	 * We are filling in the PERSISTENT RESERVATION TYPE MASK below, so
	 * set the TMV: Task Mask Valid bit.
	 */
	buf[3] |= 0x80;
	/*
	 * Change ALLOW COMMANDS to 0x20 or 0x40 later from Table 166
	 */
	buf[3] |= 0x10; /* ALLOW COMMANDS field 001b */
	/*
	 * PTPL_A: Persistence across Target Power Loss Active bit
	 */
	buf[3] |= 0x01;
	/*
	 * Setup the PERSISTENT RESERVATION TYPE MASK from Table 167
	 */
	buf[4] |= 0x80; /* PR_TYPE_EXCLUSIVE_ACCESS_ALLREG */
	buf[4] |= 0x40; /* PR_TYPE_EXCLUSIVE_ACCESS_REGONLY */
	buf[4] |= 0x20; /* PR_TYPE_WRITE_EXCLUSIVE_REGONLY */
	buf[4] |= 0x08; /* PR_TYPE_EXCLUSIVE_ACCESS */
	buf[4] |= 0x02; /* PR_TYPE_WRITE_EXCLUSIVE */
	buf[5] |= 0x01; /* PR_TYPE_EXCLUSIVE_ACCESS_ALLREG */

	return TCM_NO_SENSE;
}

static sense_reason_t
tcm_rbd_execute_pr_read_full_status(struct se_cmd *cmd, unsigned char *buf,
				    u32 buf_len)
{
	pr_err("READ FULL STATUS not supported by RBD backend\n");
	return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
}

/* handle PR registration for a currently unregistered I_T nexus */
static sense_reason_t
tcm_rbd_execute_pr_register_new(struct tcm_rbd_pr_info *pr_info, u64 old_key,
				u64 new_key, char *it_nexus,
				bool ignore_existing)
{
	sense_reason_t ret;
	int rc;

	dout("PR registration for unregistered nexus: %s\n", it_nexus);

	if (!ignore_existing && (old_key != 0)) {
		ret = TCM_RESERVATION_CONFLICT;
		goto out;
	}
	if (new_key == 0) {
		ret = TCM_NO_SENSE;
		goto out;
	}
	/*
	 * Register the I_T nexus on which the command was received with
	 * the value specified in the SERVICE ACTION RESERVATION KEY
	 * field.
	 */
	rc = tcm_rbd_pr_info_append_reg(pr_info, it_nexus, new_key);
	if (rc < 0) {
		ret = TCM_OUT_OF_RESOURCES;
		goto out;
	}

	ret = TCM_NO_SENSE;
out:
	return ret;
}

/* handle PR registration for a currently registered I_T nexus */
static sense_reason_t
tcm_rbd_execute_pr_register_existing(struct tcm_rbd_pr_info *pr_info,
				     u64 old_key, u64 new_key, char *it_nexus,
				     struct tcm_rbd_pr_reg *existing_reg,
				     bool ignore_existing)
{
	sense_reason_t ret;
	int rc;

	dout("PR registration for registered nexus: %s\n", it_nexus);

	if (!ignore_existing && (old_key != existing_reg->key)) {
		ret = TCM_RESERVATION_CONFLICT;
		goto out;
	}

	if (new_key == 0) {
		/* unregister */
		rc = tcm_rbd_pr_info_unregister_reg(pr_info,
						    existing_reg);
		if (rc < 0) {
			ret = TCM_OUT_OF_RESOURCES;
			goto out;
		}
	} else {
		/* update key */
		existing_reg->key = new_key;
	}

	ret = TCM_NO_SENSE;
out:
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_register(struct se_cmd *cmd, u64 old_key,
			    u64 new_key, bool aptpl, bool all_tg_pt,
			    bool spec_i_pt, bool ignore_existing)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *existing_reg;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (!aptpl) {
		/*
		 * Currently unsupported by block layer API (hch):
		 * reservations not persistent through a power loss are
		 * basically useless, so I decided to force them on in the API.
		 */
		pr_warn("PR register with aptpl unset. Treating as aptpl=1\n");
		aptpl = true;
	}

	if (all_tg_pt || spec_i_pt) {
		/* TODO: Currently unsupported by block layer API. */
		pr_err("failing PR register with all_tg_pt=%d spec_i_pt=%d\n",
			 all_tg_pt, spec_i_pt);
		return TCM_INVALID_CDB_FIELD;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	dout("generated nexus: %s\n", nexus_buf);

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if ((rc == -ENODATA) && (retries == 0)) {
		pr_warn("PR info not present, initializing\n");
		rc = tcm_rbd_pr_info_init(tcm_rbd_dev, &pr_info, &pr_xattr,
					  &pr_xattr_len);
	}
	if (rc < 0) {
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	/* check for an existing registration */
	existing_reg = NULL;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		if (!strncmp(reg->it_nexus, nexus_buf, ARRAY_SIZE(nexus_buf))) {
			dout("found existing PR reg for %s\n", nexus_buf);
			existing_reg = reg;
			break;
		}
	}

	if (!existing_reg) {
		ret = tcm_rbd_execute_pr_register_new(pr_info, old_key, new_key,
						      nexus_buf,
						      ignore_existing);
	} else {
		ret = tcm_rbd_execute_pr_register_existing(pr_info, old_key,
							   new_key, nexus_buf,
							   existing_reg,
							   ignore_existing);
	}
	if (ret) {
		goto err_out;
	}

	/*
	 * The Persistent Reservations Generation (PRGENERATION) field shall
	 * contain the value of a 32-bit wrapping counter that the device server
	 * shall update (e.g., increment) during the processing of any
	 * PERSISTENT RESERVE OUT command as described in table 216 (see
	 * 6.16.2). The PRgeneration value shall not be updated by a PERSISTENT
	 * RESERVE IN command or by a PERSISTENT RESERVE OUT command that is
	 * terminated due to an error or reservation conflict.
	 */
	pr_info->gen++;
	/*
	 * TODO:
	 * Regardless of the APTPL bit value the PRgeneration value shall be set
	 * to zero by a power on.
	 */

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		char *pr_xattr_changed = NULL;
		int pr_xattr_changed_len = 0;
		/* PR info has changed since we read it */
		rc = rbd_dev_getxattr(tcm_rbd_dev->rbd_dev,
				      TCM_RBD_PR_INFO_XATTR_KEY,
				      TCM_RBD_PR_INFO_XATTR_MAX_SIZE,
				      (void **)&pr_xattr_changed,
				      &pr_xattr_changed_len);
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s, now(%d) %s\n",
			pr_xattr_len, pr_xattr, pr_xattr_changed_len,
			pr_xattr_changed);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_out;
	}

	ret = TCM_NO_SENSE;
err_out:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_reserve(struct se_cmd *cmd, int type, u64 key)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *existing_reg;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if (rc < 0) {
		/* existing registration required for reservation */
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	/* check for an existing registration */
	existing_reg = NULL;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		if (!strncmp(reg->it_nexus, nexus_buf, ARRAY_SIZE(nexus_buf))) {
			dout("found existing PR reg for %s\n", nexus_buf);
			existing_reg = reg;
			break;
		}
	}

	if (!existing_reg) {
		pr_err("SPC-3 PR: Unable to locate registration for RESERVE\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (key != existing_reg->key) {
		pr_err("SPC-3 PR RESERVE: Received res_key: 0x%016Lx"
			" does not match existing SA REGISTER res_key:"
			" 0x%016Lx\n", key, existing_reg->key);
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	}

	if (pr_info->rsv) {
		if (!tcm_rbd_is_rsv_holder(pr_info->rsv, existing_reg, NULL)) {
			pr_err("SPC-3 PR: Attempted RESERVE from %s while"
			       " reservation already held by %s, returning"
			       " RESERVATION_CONFLICT\n",
			       nexus_buf, pr_info->rsv->it_nexus);
			ret = TCM_RESERVATION_CONFLICT;
			goto err_info_free;
		}

		if (pr_info->rsv->type != type) {
			/* scope already checked */
			pr_err("SPC-3 PR: Attempted RESERVE from %s trying to "
			       "change TYPE, returning RESERVATION_CONFLICT\n",
			       existing_reg->it_nexus);
			ret = TCM_RESERVATION_CONFLICT;
			goto err_info_free;
		}

		dout("reserve matches existing reservation, nothing to do\n");
		goto done;
	}

	/* new reservation */
	rc = tcm_rbd_pr_info_rsv_set(pr_info, key, nexus_buf, type);
	if (rc < 0) {
		pr_err("failed to set PR info reservation\n");
		ret = TCM_OUT_OF_RESOURCES;
		goto err_info_free;
	}

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

done:
	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_release(struct se_cmd *cmd, int type, u64 key)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *existing_reg;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if (rc < 0) {
		/* existing registration required for release */
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (!pr_info->rsv) {
		/* no persistent reservation, return GOOD status */
		goto done;
	}

	/* check for an existing registration */
	existing_reg = NULL;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		if (!strncmp(reg->it_nexus, nexus_buf, ARRAY_SIZE(nexus_buf))) {
			dout("found existing PR reg for %s\n", nexus_buf);
			existing_reg = reg;
			break;
		}
	}

	if (!existing_reg) {
		pr_err("SPC-3 PR: Unable to locate registration for RELEASE\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (!tcm_rbd_is_rsv_holder(pr_info->rsv, existing_reg, NULL)) {
		/* registered but not a persistent reservation holder */
		goto done;
	}

	if (key != existing_reg->key) {
		pr_err("SPC-3 PR RELEASE: Received res_key: 0x%016Lx"
			" does not match existing SA REGISTER res_key:"
			" 0x%016Lx\n", key, existing_reg->key);
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	}

	if (pr_info->rsv->type != type) {
		pr_err("SPC-3 PR: Attempted RELEASE from %s with different "
		       "TYPE, returning RESERVATION_CONFLICT\n",
		       existing_reg->it_nexus);
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	}

	/* release the persistent reservation */
	tcm_rbd_pr_info_rsv_clear(pr_info);

	/*
	 * TODO:
	 * c) If the released persistent reservation is a registrants only type
	 * or all registrants type persistent reservation,
	 *    the device server shall establish a unit attention condition for
	 *    the initiator port associated with every regis-
	 *    tered I_T nexus other than I_T nexus on which the PERSISTENT
	 *    RESERVE OUT command with RELEASE service action was received,
	 *    with the additional sense code set to RESERVATIONS RELEASED
	 */

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

done:
	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_clear(struct se_cmd *cmd, u64 key)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *existing_reg;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if (rc < 0) {
		/* existing registration required for clear */
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	/* check for an existing registration */
	existing_reg = NULL;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		if (!strncmp(reg->it_nexus, nexus_buf, ARRAY_SIZE(nexus_buf))) {
			dout("found existing PR reg for %s\n", nexus_buf);
			existing_reg = reg;
			break;
		}
	}

	if (!existing_reg) {
		pr_err("SPC-3 PR: Unable to locate registration for CLEAR\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (key != existing_reg->key) {
		pr_err("SPC-3 PR CLEAR: Received res_key: 0x%016Lx"
			" does not match existing SA REGISTER res_key:"
			" 0x%016Lx\n", key, existing_reg->key);
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	}

	/* release the persistent reservation, if any */
	if (pr_info->rsv)
		tcm_rbd_pr_info_rsv_clear(pr_info);

	/* remove all registrations */
	list_for_each_entry_safe(existing_reg, reg, &pr_info->regs, regs_node) {
		tcm_rbd_pr_info_clear_reg(pr_info, existing_reg);
	}

	/*
	 * TODO:
	 * e) Establish a unit attention condition for the initiator
	 *    port associated with every registered I_T nexus other
	 *    than the I_T nexus on which the PERSISTENT RESERVE OUT
	 *    command with CLEAR service action was received, with the
	 *    additional sense code set to RESERVATIONS PREEMPTED.
	 */

	/* PR generation must be incremented on successful CLEAR */
	pr_info->gen++;

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static int
tcm_rbd_pr_info_rm_regs_key(struct tcm_rbd_pr_info *pr_info,
			    struct tcm_rbd_pr_reg *existing_reg,
			    u64 new_key)
{
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *reg_n;
	bool found = false;

	if (new_key == 0) {
		dout("removing all non-nexus regs\n");
	}

	list_for_each_entry_safe(reg, reg_n, &pr_info->regs, regs_node) {
		if (reg == existing_reg)
			continue;

		if (new_key && (reg->key != new_key))
			continue;

		tcm_rbd_pr_info_clear_reg(pr_info, reg);
		found = true;

		/* TODO flag UA if different IT nexus */
	}

	if (!found) {
		return -ENOENT;
	}

	return 0;
}

/*
 * Preempt logic is pretty complex. This implementation attempts to resemble
 * SPC4r37 Figure 9 — Device server interpretation of PREEMPT service action.
 */
static sense_reason_t
tcm_rbd_execute_pr_preempt(struct se_cmd *cmd, u64 old_key, u64 new_key,
			   int type, bool abort)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	struct tcm_rbd_pr_rsv *rsv;
	struct tcm_rbd_pr_reg *reg;
	struct tcm_rbd_pr_reg *existing_reg;
	bool all_reg;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (abort) {
		pr_err("PR PREEMPT AND ABORT not supported by RBD backend\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	all_reg = false;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if (rc == -ENODATA) {
		pr_err("SPC-3 PR: no registrations for PREEMPT\n");
		return TCM_RESERVATION_CONFLICT;
	} else if (rc < 0) {
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	/* check for an existing registration */
	existing_reg = NULL;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		if (!strncmp(reg->it_nexus, nexus_buf, ARRAY_SIZE(nexus_buf))) {
			dout("found existing PR reg for %s\n", nexus_buf);
			existing_reg = reg;
			break;
		}
	}

	if (!existing_reg) {
		pr_err("SPC-3 PR: Unable to locate registration for PREEMPT\n");
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	}

	if (old_key != existing_reg->key) {
		pr_err("SPC-3 PR PREEMPT: Received res_key: 0x%016Lx"
			" does not match existing SA REGISTER res_key:"
			" 0x%016Lx\n", old_key, existing_reg->key);
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	}

	if (!pr_info->rsv) {
		/* no reservation, remove regs indicated by new_key */
		if (new_key == 0) {
			ret = TCM_INVALID_PARAMETER_LIST;
			goto err_info_free;
		}
		rc = tcm_rbd_pr_info_rm_regs_key(pr_info, existing_reg,
						 new_key);
		if (rc == -ENOENT) {
			ret = TCM_RESERVATION_CONFLICT;
			goto err_info_free;
		} else if (rc < 0) {
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			goto err_info_free;
		}
		goto commit;
	}


	rsv = pr_info->rsv;
	if ((rsv->type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG)
			|| (rsv->type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)) {
		all_reg = true;
	}

	if (all_reg) {
		/* if key is zero, then remove all non-nexus regs */
		rc = tcm_rbd_pr_info_rm_regs_key(pr_info, existing_reg,
						 new_key);
		if (rc == -ENOENT) {
			ret = TCM_RESERVATION_CONFLICT;
			goto err_info_free;
		} else if (rc < 0) {
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			goto err_info_free;
		}

		if (new_key == 0) {
			tcm_rbd_pr_info_rsv_clear(pr_info);
			rsv = NULL;
			rc = tcm_rbd_pr_info_rsv_set(pr_info, existing_reg->key,
						     existing_reg->it_nexus, type);
			if (rc < 0) {
				pr_err("failed to set PR info reservation\n");
				ret = TCM_OUT_OF_RESOURCES;
				goto err_info_free;
			}
		}
		goto commit;
	}

	if (rsv->key != new_key) {
		if (new_key == 0) {
			ret = TCM_INVALID_PARAMETER_LIST;
			goto err_info_free;
		}
		rc = tcm_rbd_pr_info_rm_regs_key(pr_info, existing_reg,
						 new_key);
		if (rc == -ENOENT) {
			ret = TCM_RESERVATION_CONFLICT;
			goto err_info_free;
		} else if (rc < 0) {
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			goto err_info_free;
		}
		goto commit;
	}

	rc = tcm_rbd_pr_info_rm_regs_key(pr_info, existing_reg, new_key);
	if (rc == -ENOENT) {
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	} else if (rc < 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}
	tcm_rbd_pr_info_rsv_clear(pr_info);
	rsv = NULL;
	rc = tcm_rbd_pr_info_rsv_set(pr_info, existing_reg->key,
				     existing_reg->it_nexus, type);
	if (rc < 0) {
		pr_err("failed to set PR info reservation\n");
		ret = TCM_OUT_OF_RESOURCES;
		goto err_info_free;
	}
commit:
	/* PR generation must be incremented on successful PREEMPT */
	pr_info->gen++;

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_register_and_move(struct se_cmd *cmd, u64 old_key,
				     u64 new_key, bool aptpl, int unreg)
{
	pr_err("REGISTER AND MOVE not supported by RBD backend\n");
	return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
}

static sense_reason_t
tcm_rbd_execute_pr_scsi2_check_scsi3_conflict(struct tcm_rbd_pr_info *pr_info,
					      char *it_nexus)
{
	struct tcm_rbd_pr_rsv *rsv = pr_info->rsv;

	if (rsv) {
		struct tcm_rbd_pr_reg *reg;

		/*
		 * spc4r17
		 * 5.12.3 Exceptions to SPC-2 RESERVE and RELEASE behavior
		 * A RESERVE(6) command or RESERVE(10) command shall complete
		 * with GOOD status, but no reservation shall be established and
		 * the persistent reservation shall not be changed, if the
		 * command is received from:
		 * a) an I_T nexus that is a persistent reservation holder; or
		 * b) an I_T nexus that is registered if a registrants only or
		 *    all registrants type persistent reservation is present.
		 */
		list_for_each_entry(reg, &pr_info->regs, regs_node) {
			if (strncmp(reg->it_nexus, it_nexus,
						ARRAY_SIZE(reg->it_nexus))) {
				continue;
			}
			dout("SCSI2 RESERVE from PR registrant: %s\n",
			     it_nexus);
			/* ALLREG types checked by tcm_rbd_is_rsv_holder() */
			if (tcm_rbd_is_rsv_holder(rsv, reg, NULL)
			      || ((rsv->type == PR_TYPE_WRITE_EXCLUSIVE_REGONLY)
			  || (rsv->type == PR_TYPE_EXCLUSIVE_ACCESS_REGONLY))) {
				return 1;
			}
		}
	}

	if (pr_info->num_regs > 0) {
		/*
		 * Following spc2r20 5.5.1 Reservations overview:
		 *
		 * If a logical unit has executed a PERSISTENT RESERVE OUT
		 * command with the REGISTER or the REGISTER AND IGNORE
		 * EXISTING KEY service action and is still registered by any
		 * initiator, all RESERVE commands and all RELEASE commands
		 * regardless of initiator shall conflict and shall terminate
		 * with a RESERVATION CONFLICT status.
		 */
		pr_err("Received legacy SPC-2 RESERVE/RELEASE"
			" while active SPC-3 registrations exist,"
			" returning RESERVATION_CONFLICT\n");
		return -EBUSY;
	}

	return 0;
}

static sense_reason_t
tcm_rbd_execute_pr_scsi2_reserve(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SCSI2 RESERVE: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if ((rc == -ENODATA) && (retries == 0)) {
		pr_warn("PR info not present, initializing\n");
		rc = tcm_rbd_pr_info_init(tcm_rbd_dev, &pr_info, &pr_xattr,
					  &pr_xattr_len);
	}
	if (rc < 0) {
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_execute_pr_scsi2_check_scsi3_conflict(pr_info, nexus_buf);
	if (rc == -EBUSY) {
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	} else if (rc < 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	} else if (rc == 1) {
		/* return GOOD without processing request */
		goto done;
	}

	if (pr_info->scsi2_rsv) {
		if (strncmp(pr_info->scsi2_rsv->it_nexus,
			    nexus_buf, ARRAY_SIZE(nexus_buf))) {
			dout("SCSI2 reservation conflict: held by %s\n",
			     pr_info->scsi2_rsv->it_nexus);
			ret = TCM_RESERVATION_CONFLICT;
			goto err_info_free;
		}
		dout("SCSI2 reservation already held by %s\n",
		     nexus_buf);
		goto done;
	}

	dout("new SCSI2 reservation\n");
	ret = tcm_rbd_pr_info_scsi2_rsv_set(pr_info, nexus_buf);
	if (ret < 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

done:
	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_scsi2_release(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	struct tcm_rbd_pr_info *pr_info;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

	if (!cmd->se_sess || !cmd->se_lun) {
		pr_err("SCSI2 RESERVE: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if ((rc == -ENODATA) && (retries == 0)) {
		dout("PR info not present for SCSI2 release\n");
		return TCM_NO_SENSE;
	}
	if (rc < 0) {
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_execute_pr_scsi2_check_scsi3_conflict(pr_info, nexus_buf);
	if (rc == -EBUSY) {
		ret = TCM_RESERVATION_CONFLICT;
		goto err_info_free;
	} else if (rc < 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	} else if (rc == 1) {
		/* return GOOD without processing request */
		goto done;
	}

	if (!pr_info->scsi2_rsv || strncmp(pr_info->scsi2_rsv->it_nexus,
					   nexus_buf, ARRAY_SIZE(nexus_buf))) {
		dout("SCSI2 release against non-matching reservation\n");
		goto done;
	}

	tcm_rbd_pr_info_scsi2_rsv_clear(pr_info);

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

done:
	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_check_scsi2_conflict(struct tcm_rbd_pr_info *pr_info,
					char *it_nexus,
					enum target_pr_check_type type)
{
	if (!pr_info->scsi2_rsv) {
		dout("no SCSI2 reservation\n");
		return TCM_NO_SENSE;
	}

	if (type == TARGET_PR_CHECK_SCSI2_ANY) {
		dout("SCSI2 reservation conflict: %s with ANY\n",
		     it_nexus);
		return TCM_RESERVATION_CONFLICT;
	}

	if (strncmp(pr_info->scsi2_rsv->it_nexus, it_nexus,
		    ARRAY_SIZE(pr_info->scsi2_rsv->it_nexus))) {
		dout("SCSI2 reservation conflict: %s with %s holder\n",
		     it_nexus, pr_info->scsi2_rsv->it_nexus);
		return TCM_RESERVATION_CONFLICT;
	}

	return TCM_NO_SENSE;
}

static sense_reason_t
tcm_rbd_execute_pr_check_scsi3_conflict(struct se_cmd *cmd,
					struct tcm_rbd_pr_info *pr_info,
					char *it_nexus)
{
	struct tcm_rbd_pr_rsv *rsv;
	struct tcm_rbd_pr_reg *reg;
	bool registered_nexus;
	int rc;

	if (!pr_info->rsv) {
		dout("no SCSI3 persistent reservation\n");
		return TCM_NO_SENSE;
	}

	rsv = pr_info->rsv;
	dout("PR reservation holder: %s, us: %s\n", rsv->it_nexus, it_nexus);

	if (!strncmp(rsv->it_nexus, it_nexus, ARRAY_SIZE(rsv->it_nexus))) {
		dout("cmd is from reservation holder\n");
		return TCM_NO_SENSE;
	}

	registered_nexus = false;
	list_for_each_entry(reg, &pr_info->regs, regs_node) {
		if (!strncmp(reg->it_nexus, it_nexus,
						ARRAY_SIZE(reg->it_nexus))) {
			dout("cmd is from PR registrant: %s\n", it_nexus);
			registered_nexus = true;
			break;
		}
	}
	rc = core_scsi3_pr_seq_non_holder(cmd, rsv->type, it_nexus,
					  registered_nexus);
	if (rc == 1) {
		dout("SCSI3 reservation conflict\n");
		return TCM_RESERVATION_CONFLICT;
	} else if (rc < 0) {
		pr_warn("SCSI3 PR non-holder check failed\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	return TCM_NO_SENSE;
}

static sense_reason_t
tcm_rbd_execute_pr_check_conflict(struct se_cmd *cmd,
				  enum target_pr_check_type type)
{
	struct se_device *dev = cmd->se_dev;
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct tcm_rbd_pr_info *pr_info;
	char nexus_buf[TCM_RBD_PR_IT_NEXUS_MAXLEN];
	int rc;
	sense_reason_t ret;

	switch (cmd->t_task_cdb[0]) {
	case INQUIRY:
	case RELEASE:
	case RELEASE_10:
		/* always allow cdb execution */
		return TCM_NO_SENSE;
	default:
		break;
	}

	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, NULL, NULL);
	if (rc == -ENODATA) {
		dout("no PR info, can't conflict\n");
		return TCM_NO_SENSE;
	}
	if (rc < 0) {
		/* existing registration required for reservation */
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	rc = tcm_rbd_gen_it_nexus(cmd->se_sess, nexus_buf,
				  ARRAY_SIZE(nexus_buf));
	if (rc < 0) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto out_info_free;
	}

	ret = tcm_rbd_execute_pr_check_scsi2_conflict(pr_info, nexus_buf, type);
	if (ret || (type == TARGET_PR_CHECK_SCSI2_ANY)) {
		/* SCSI2 conflict/failure, or caller only interested in SCSI2 */
		goto out_info_free;
	}

	ret = tcm_rbd_execute_pr_check_scsi3_conflict(cmd, pr_info, nexus_buf);
	if (ret)
		goto out_info_free;

	ret = TCM_NO_SENSE;
out_info_free:
	tcm_rbd_pr_info_free(pr_info);
	return ret;
}

static sense_reason_t
tcm_rbd_execute_pr_reset(struct se_device *dev)
{
	struct tcm_rbd_dev *tcm_rbd_dev = TCM_RBD_DEV(dev);
	struct tcm_rbd_pr_info *pr_info;
	char *pr_xattr;
	int pr_xattr_len;
	int rc;
	sense_reason_t ret;
	int retries = 0;

retry:
	pr_info = NULL;
	pr_xattr = NULL;
	pr_xattr_len = 0;
	rc = tcm_rbd_pr_info_get(tcm_rbd_dev, &pr_info, &pr_xattr,
				 &pr_xattr_len);
	if ((rc == -ENODATA) && (retries == 0)) {
		dout("PR info not present for reset\n");
		return TCM_NO_SENSE;
	}
	if (rc < 0) {
		pr_err("failed to obtain PR info\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	if (!pr_info->scsi2_rsv) {
		dout("no SCSI2 reservation to clear for reset");
		goto done;
	}

	tcm_rbd_pr_info_scsi2_rsv_clear(pr_info);

	rc = tcm_rbd_pr_info_replace(tcm_rbd_dev, pr_xattr, pr_xattr_len,
				     pr_info);
	if (rc == -ECANCELED) {
		pr_warn("atomic PR info update failed due to parallel "
			"change, expected(%d) %s. Retrying...\n",
			pr_xattr_len, pr_xattr);
		retries++;
		if (retries <= TCM_RBD_PR_REG_MAX_RETRIES) {
			tcm_rbd_pr_info_free(pr_info);
			kfree(pr_xattr);
			goto retry;
		}
	}
	if (rc < 0) {
		pr_err("atomic PR info update failed: %d\n", rc);
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err_info_free;
	}

	dout("cleared SCSI2 reservation on reset\n");

done:
	ret = TCM_NO_SENSE;
err_info_free:
	tcm_rbd_pr_info_free(pr_info);
	kfree(pr_xattr);
	return ret;
}

static struct target_pr_ops tcm_rbd_pr_ops = {
	.check_conflict		= tcm_rbd_execute_pr_check_conflict,
	.scsi2_reserve		= tcm_rbd_execute_pr_scsi2_reserve,
	.scsi2_release		= tcm_rbd_execute_pr_scsi2_release,
	.reset			= tcm_rbd_execute_pr_reset,

	.pr_read_keys		= tcm_rbd_execute_pr_read_keys,
	.pr_read_reservation	= tcm_rbd_execute_pr_read_reservation,
	.pr_report_capabilities	= tcm_rbd_execute_pr_report_capabilities,
	.pr_read_full_status	= tcm_rbd_execute_pr_read_full_status,

	.pr_register		= tcm_rbd_execute_pr_register,
	.pr_reserve		= tcm_rbd_execute_pr_reserve,
	.pr_release		= tcm_rbd_execute_pr_release,
	.pr_clear		= tcm_rbd_execute_pr_clear,
	.pr_preempt		= tcm_rbd_execute_pr_preempt,
	.pr_register_and_move	= tcm_rbd_execute_pr_register_and_move,
};

static const struct target_backend_ops tcm_rbd_ops = {
	.name				= "rbd",
	.inquiry_prod			= "RBD",
	.inquiry_rev			= TCM_RBD_VERSION,
	.owner				= THIS_MODULE,
	.attach_hba			= tcm_rbd_attach_hba,
	.detach_hba			= tcm_rbd_detach_hba,
	.alloc_device			= tcm_rbd_alloc_device,
	.configure_device		= tcm_rbd_configure_device,
	.free_device			= tcm_rbd_free_device,
	.parse_cdb			= tcm_rbd_parse_cdb,
	.set_configfs_dev_params	= tcm_rbd_set_configfs_dev_params,
	.show_configfs_dev_params	= tcm_rbd_show_configfs_dev_params,
	.get_device_type		= sbc_get_device_type,
	.get_blocks			= tcm_rbd_get_blocks,
	.get_alignment_offset_lbas	= tcm_rbd_get_alignment_offset_lbas,
	.get_lbppbe			= tcm_rbd_get_lbppbe,
	.get_io_min			= tcm_rbd_get_io_min,
	.get_io_opt			= tcm_rbd_get_io_opt,
	.get_write_cache		= tcm_rbd_get_write_cache,
	.pr_ops				= &tcm_rbd_pr_ops,
	.tb_dev_attrib_attrs		= sbc_attrib_attrs,
};

static int __init tcm_rbd_module_init(void)
{
	return transport_backend_register(&tcm_rbd_ops);
}

static void __exit tcm_rbd_module_exit(void)
{
	target_backend_unregister(&tcm_rbd_ops);
}

MODULE_DESCRIPTION("TCM Ceph RBD subsystem plugin");
MODULE_AUTHOR("Mike Christie");
MODULE_LICENSE("GPL");

module_init(tcm_rbd_module_init);
module_exit(tcm_rbd_module_exit);
