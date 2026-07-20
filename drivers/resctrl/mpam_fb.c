// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2024 Arm Ltd.

#include <linux/arm_mpam.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/processor.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <acpi/pcc.h>

#include <asm/mpam.h>

#include "mpam_internal.h"

#define MPAM_FB_PROTOCOL_ID	0x1a
#define MPAM_PROTOCOL_VERSION	0x0
#define MPAM_MSC_ATTRIBUTES_CMD	0x3
#define MPAM_MSC_READ_CMD	0x4
#define MPAM_MSC_WRITE_CMD	0x5

#define MPAM_FB_ERR_SUCCESS		 0
#define MPAM_FB_ERR_NOT_SUPPORTED	-1
#define MPAM_FB_ERR_INVALID_PARAMETERS	-2
#define MPAM_FB_ERR_DENIED		-3
#define MPAM_FB_ERR_NOT_FOUND		-4
#define MPAM_FB_ERR_OUT_OF_RANGE	-5
#define MPAM_FB_ERR_BUSY		-6
#define MPAM_FB_ERR_COMMS_ERROR		-7
#define MPAM_FB_ERR_GENERIC_ERROR	-8
#define MPAM_FB_ERR_HW_ERROR		-9
#define MPAM_FB_ERR_PROTOCOL_ERROR	-10
#define MPAM_FB_ERR_IN_USE		-11

#define MPAM_MSC_PROT_ID_MASK	GENMASK(17, 10)
#define MPAM_MSC_TOKEN_MASK	GENMASK(27, 18)

struct mpam_fb_access_payload {
	u32 msc_id;
	u32 flags;
	u32 reg_offset;
	u32 value;
} __packed;

#define PCC_CHAN_FLAGS_IRQ	BIT(0)
#define MPAM_VERSION_MSG_SIZE	(PCC_TYPE3_MSG_PAYLOAD_OFS)
#define MPAM_READ_MSG_SIZE	(PCC_TYPE3_MSG_PAYLOAD_OFS + 3 * sizeof(u32))
#define MPAM_WRITE_MSG_SIZE	(PCC_TYPE3_MSG_PAYLOAD_OFS + 4 * sizeof(u32))

static atomic_t mpam_fb_token = ATOMIC_INIT(0);

static int mpam_fb_build_version_message(unsigned int token,
					 void __iomem *msg_buf)
{
	struct acpi_pcct_ext_pcc_shared_memory *pcc_shmem = msg_buf;

	writel_relaxed(0, &pcc_shmem->flags);
	writel_relaxed(MPAM_VERSION_MSG_SIZE, &pcc_shmem->length);
	writel_relaxed(MPAM_PROTOCOL_VERSION |
		       FIELD_PREP(MPAM_MSC_TOKEN_MASK, token) |
		       FIELD_PREP(MPAM_MSC_PROT_ID_MASK, MPAM_FB_PROTOCOL_ID),
		       &pcc_shmem->command);

	return MPAM_VERSION_MSG_SIZE;
}

static int mpam_fb_build_read_message(int msc_id, int reg, unsigned int token,
				      void __iomem *msg_buf)
{
	struct acpi_pcct_ext_pcc_shared_memory *pcc_shmem = msg_buf;
	struct mpam_fb_access_payload *payload = msg_buf + sizeof(*pcc_shmem);

	writel_relaxed(0, &pcc_shmem->flags);
	writel_relaxed(MPAM_READ_MSG_SIZE, &pcc_shmem->length);
	writel_relaxed(MPAM_MSC_READ_CMD |
		       FIELD_PREP(MPAM_MSC_TOKEN_MASK, token) |
		       FIELD_PREP(MPAM_MSC_PROT_ID_MASK, MPAM_FB_PROTOCOL_ID),
		       &pcc_shmem->command);

	writel_relaxed(msc_id, &payload->msc_id);
	writel_relaxed(0, &payload->flags);
	writel_relaxed(reg, &payload->reg_offset);

	return MPAM_READ_MSG_SIZE;
}

static int mpam_fb_build_write_message(int msc_id, int reg, u32 val,
				       unsigned int token,
				       void __iomem *msg_buf)
{
	struct acpi_pcct_ext_pcc_shared_memory *pcc_shmem = msg_buf;
	struct mpam_fb_access_payload *payload = msg_buf + sizeof(*pcc_shmem);

	writel_relaxed(0, &pcc_shmem->flags);
	writel_relaxed(MPAM_WRITE_MSG_SIZE, &pcc_shmem->length);
	writel_relaxed(MPAM_MSC_WRITE_CMD |
		       FIELD_PREP(MPAM_MSC_TOKEN_MASK, token) |
		       FIELD_PREP(MPAM_MSC_PROT_ID_MASK, MPAM_FB_PROTOCOL_ID),
		       &pcc_shmem->command);

	writel_relaxed(msc_id, &payload->msc_id);
	writel_relaxed(0, &payload->flags);
	writel_relaxed(reg, &payload->reg_offset);
	writel_relaxed(val, &payload->value);

	return MPAM_WRITE_MSG_SIZE;
}

static int mpam_fb_send_request(struct mpam_pcc_chan *pcc_chan, u32 msc_id,
				u16 reg, u32 *result, int mpam_fb_command)
{
	unsigned int token = atomic_inc_return(&mpam_fb_token);
	struct acpi_pcct_ext_pcc_shared_memory *pcc_shmem;
	struct pcc_mbox_chan *chan;
	void __iomem *payload_ofs;
	u32 status;
	int ret;

	if (!pcc_chan)
		return -ENODEV;

	chan = pcc_chan->pcc_chan;

	guard(mutex)(&pcc_chan->pcc_chan_lock);

	switch (mpam_fb_command) {
	case MPAM_MSC_WRITE_CMD:
		ret = mpam_fb_build_write_message(msc_id, reg, *result,
						  token, chan->shmem);
		break;
	case MPAM_MSC_READ_CMD:
		ret = mpam_fb_build_read_message(msc_id, reg,
						 token, chan->shmem);
		break;
	case MPAM_PROTOCOL_VERSION:
		ret = mpam_fb_build_version_message(token, chan->shmem);
		break;
	}
	if (ret < 0)
		return ret;

	ret = mbox_send_message(chan->mchan, NULL);
	if (ret < 0)
		return ret;

	pcc_shmem = chan->shmem;
	payload_ofs = chan->shmem + sizeof(*pcc_shmem);
	status = readl(&pcc_shmem->command);
	if (FIELD_GET(MPAM_MSC_TOKEN_MASK, status) != token)
		return -ETIMEDOUT;

	ret = readl(payload_ofs + 0x0);
	if (ret < 0) {
		switch (ret) {
		case MPAM_FB_ERR_NOT_SUPPORTED:
			return -EOPNOTSUPP;
		case MPAM_FB_ERR_INVALID_PARAMETERS:
			return -EINVAL;
		case MPAM_FB_ERR_NOT_FOUND:
			return -ENOENT;
		case MPAM_FB_ERR_OUT_OF_RANGE:
			return -ERANGE;
		default:
			return -EINVAL;
		}
	}

	if (mpam_fb_command != MPAM_MSC_WRITE_CMD)
		*result = readl(payload_ofs + 0x4);

	return 0;
}

int mpam_fb_send_read_request(struct mpam_msc *msc, u16 reg, u32 *result)
{
	return mpam_fb_send_request(msc->pcc_chan, msc->mpam_fb_msc_id,
				    reg, result, MPAM_MSC_READ_CMD);
}

int mpam_fb_send_write_request(struct mpam_msc *msc, u16 reg, u32 value)
{
	return mpam_fb_send_request(msc->pcc_chan, msc->mpam_fb_msc_id,
				    reg, &value, MPAM_MSC_WRITE_CMD);
}

int mpam_fb_get_protocol_version(struct mpam_msc *msc)
{
	u32 version;
	int ret;

	ret = mpam_fb_send_request(msc->pcc_chan, 0,
				   0, &version, MPAM_PROTOCOL_VERSION);
	if (ret)
		return ret;

	return version;
}
