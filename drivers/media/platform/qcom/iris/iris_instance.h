/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_INSTANCE_H__
#define __IRIS_INSTANCE_H__

#include <media/v4l2-ctrls.h>

#include "iris_buffer.h"
#include "iris_core.h"
#include "iris_utils.h"

#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240

enum iris_fmt_type {
	IRIS_FMT_H264,
	IRIS_FMT_HEVC,
	IRIS_FMT_VP9,
};

struct iris_fmt {
	u32 pixfmt;
	u32 type;
};

/**
 * struct iris_inst - holds per video instance parameters
 *
 * @list: used for attach an instance to the core
 * @core: pointer to core structure
 * @session_id: id of current video session
 * @ctx_q_lock: lock to serialize queues related ioctls
 * @lock: lock to seralise forward and reverse threads
 * @fh: reference of v4l2 file handler
 * @fmt_src: structure of v4l2_format for source
 * @fmt_dst: structure of v4l2_format for destination
 * @ctrl_handler: reference of v4l2 ctrl handler
 * @domain: domain type: encoder or decoder
 * @crop: structure of crop info
 * @compose: structure of compose info
 * @completion: structure of signal completions
 * @flush_completion: structure of signal completions for flush cmd
 * @flush_responses_pending: counter to track number of pending flush responses
 * @fw_caps: array of supported instance firmware capabilities
 * @buffers: array of different iris buffers
 * @fw_min_count: minimnum count of buffers needed by fw
 * @state: instance state
 * @sub_state: instance sub state
 * @once_per_session_set: boolean to set once per session property
 * @max_input_data_size: max size of input data
 * @power: structure of power info
 * @icc_data: structure of interconnect data
 * @m2m_dev:	a reference to m2m device structure
 * @m2m_ctx:	a reference to m2m context structure
 * @sequence_cap: a sequence counter for capture queue
 * @sequence_out: a sequence counter for output queue
 * @tss: timestamp metadata
 * @metadata_idx: index for metadata buffer
 * @codec: codec type
 * @last_buffer_dequeued: a flag to indicate that last buffer is sent by driver
 * @frame_rate: frame rate of current instance
 * @operating_rate: operating rate of current instance
 * @hfi_rc_type: rate control type
 */

struct iris_inst {
	struct list_head		list;
	struct iris_core		*core;
	u32				session_id;
	struct mutex			ctx_q_lock;/* lock to serialize queues related ioctls */
	struct mutex			lock; /* lock to serialize forward and reverse threads */
	struct v4l2_fh			fh;
	struct v4l2_format		*fmt_src;
	struct v4l2_format		*fmt_dst;
	struct v4l2_ctrl_handler	ctrl_handler;
	enum domain_type		domain;
	struct iris_hfi_rect_desc	crop;
	struct iris_hfi_rect_desc	compose;
	struct completion		completion;
	struct completion		flush_completion;
	u32				flush_responses_pending;
	struct platform_inst_fw_cap	fw_caps[INST_FW_CAP_MAX];
	struct iris_buffers		buffers[BUF_TYPE_MAX];
	u32				fw_min_count;
	enum iris_inst_state		state;
	enum iris_inst_sub_state	sub_state;
	bool				once_per_session_set;
	size_t				max_input_data_size;
	struct iris_inst_power		power;
	struct icc_vote_data		icc_data;
	struct v4l2_m2m_dev		*m2m_dev;
	struct v4l2_m2m_ctx		*m2m_ctx;
	u32				sequence_cap;
	u32				sequence_out;
	struct iris_ts_metadata		tss[VIDEO_MAX_FRAME];
	u32				metadata_idx;
	u32				codec;
	bool				last_buffer_dequeued;
	u32				frame_rate;
	u32				operating_rate;
	u32				hfi_rc_type;
};

#endif
