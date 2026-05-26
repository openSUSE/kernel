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

#define GROUP(_name, _pipe, _counters, _countables) {                          \
      .name = _name,                                                           \
      .pipe = _pipe,                                                           \
      .num_counters = ARRAY_SIZE(_counters),                                   \
      .counters = _counters,                                                   \
   }

#define fd_perfcntr_counter msm_perfcntr_counter
#define fd_perfcntr_group   msm_perfcntr_group

#endif /* __MSM_PERFCNTR_H__ */
