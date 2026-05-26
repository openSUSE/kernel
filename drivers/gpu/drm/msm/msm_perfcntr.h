// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MSM_PERFCNTR_H__
#define __MSM_PERFCNTR_H__

#include "linux/array_size.h"

#include "adreno_common.xml.h"

/*
 * This is a subset of the tables used by mesa.  We don't need to
 * enumerate the countables on the kernel side.
 */

/* Describes a single counter: */
struct msm_perfcntr_counter {
	/* offset of the SELect register to choose what to count: */
	unsigned select_reg;
	/* additional SEL regs to enable slice counters (gen8+) */
	unsigned slice_select_regs[2];
	/* offset of the lo/hi 32b to read current counter value: */
	unsigned counter_reg_lo;
	unsigned counter_reg_hi;
	/* TODO some counters have enable/clear registers */
};

/* Describes an entire counter group: */
struct msm_perfcntr_group {
	const char *name;
	enum adreno_pipe pipe;
	unsigned num_counters;
	const struct msm_perfcntr_counter *counters;
};

/**
 * struct msm_perfcntr_stream - state for a single open stream fd
 */
struct msm_perfcntr_stream {
	/** @gpu: Back-link to the GPU */
	struct msm_gpu *gpu;

	/** @nr_groups: # of counter groups with enabled counters */
	uint32_t nr_groups;

	/** @sel_fence: Fence for SEL reg programming  */
	uint32_t sel_fence;

	/**
	 * @group_idx: array of nr_groups
	 *
	 * Maps the order of groups in PERFCNTR_CONFIG ioctl to group idx,
	 * so that results in the results stream can be ordered to match
	 * the ioctl call that setup the stream
	 */
	uint32_t *group_idx;
};

uint32_t msm_perfcntr_group_idx(const struct msm_perfcntr_stream *stream, uint32_t n);
uint32_t msm_perfcntr_counter_base(const struct msm_perfcntr_stream *stream, uint32_t group_idx);

/**
 * struct msm_perfcntr_context_state - per-msm_context counter state
 *
 * A given counter can either be unused, reserved for global counter
 * collection exclusively, or reserved for local per-context counter
 * collection inclusively.  Multiple contexts can reserve the same
 * counter, since SEL reg programming and counter begin/end sampling
 * happen locally (within a single GEM_SUBMIT ioctl).
 */
struct msm_perfcntr_context_state {
	/** @dummy: Some compilers dislike structs with only a flex array */
	unsigned dummy;

	/**
	 * @reserved_counters:
	 *
	 * The number of reserved counters indexed by perfcntr group.
	 */
	unsigned reserved_counters[];
};

extern const struct msm_perfcntr_group a6xx_perfcntr_groups[];
extern const unsigned a6xx_num_perfcntr_groups;

extern const struct msm_perfcntr_group a7xx_perfcntr_groups[];
extern const unsigned a7xx_num_perfcntr_groups;

extern const struct msm_perfcntr_group a8xx_perfcntr_groups[];
extern const unsigned a8xx_num_perfcntr_groups;

#define GROUP(_name, _pipe, _counters, _countables) {                          \
      .name = _name,                                                           \
      .pipe = _pipe,                                                           \
      .num_counters = ARRAY_SIZE(_counters),                                   \
      .counters = _counters,                                                   \
   }

#define fd_perfcntr_counter msm_perfcntr_counter
#define fd_perfcntr_group   msm_perfcntr_group

#endif /* __MSM_PERFCNTR_H__ */
