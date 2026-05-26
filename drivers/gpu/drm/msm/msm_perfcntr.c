// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "msm_drv.h"
#include "msm_gpu.h"
#include "msm_perfcntr.h"

static int
msm_perfcntr_resume_locked(struct msm_perfcntr_stream *stream)
{
	return 0;
}

int
msm_perfcntr_resume(struct msm_gpu *gpu)
{
	if (!gpu->perfcntrs)
		return 0;
	guard(mutex)(&gpu->perfcntr_lock);
	return msm_perfcntr_resume_locked(gpu->perfcntrs->stream);
}

static void
msm_perfcntr_suspend_locked(struct msm_perfcntr_stream *stream)
{
}

void
msm_perfcntr_suspend(struct msm_gpu *gpu)
{
	if (!gpu->perfcntrs)
		return;
	guard(mutex)(&gpu->perfcntr_lock);
	msm_perfcntr_suspend_locked(gpu->perfcntrs->stream);
}

/**
 * msm_perfcntr_group_idx - map idx of perfcntr group to group_idx
 * @stream: The global perfcntr stream
 * @n: The requested group_idx
 *
 * The PERFCNTR_CONFIG ioctl requested N counters/countables per perfcntr
 * group, but the order of groups is not required to match the order they
 * are defined in the perfcntr tables (which is not stable/UABI, only the
 * group names are UABI).
 *
 * But the order samples are returned in the stream should match the
 * order they are requested in the PERFCNTR_CONFIG ioctl.  This helper
 * handles the order remapping.
 *
 * Returns an index into gpu->perfcntr_groups[] and perfcntrs->groups[].
 */
uint32_t
msm_perfcntr_group_idx(const struct msm_perfcntr_stream *stream, uint32_t n)
{
	WARN_ON_ONCE(n >= stream->nr_groups);
	return stream->group_idx[n];
}

/**
 * msm_perfcntr_counter_base - get idx of the first counter in group
 * @stream: The global perfcntr stream
 * @group_idx: the index of the counter group
 *
 * For global counter collection, counters are allocated from the end
 * (last counter) while UMD allocates them from the start (0..N-1).
 * Since UMD always allocated them from the start this also minimizes
 * the chance of conflict when using old UMD which predates
 * PERFCNTR_CONFIG ioctl.
 *
 * Returns the index of first counter to use.  An index into
 * msm_perfcntr_group::counters[].
 */
uint32_t
msm_perfcntr_counter_base(const struct msm_perfcntr_stream *stream, uint32_t group_idx)
{
	struct msm_gpu *gpu = stream->gpu;
	struct msm_perfcntr_state *perfcntrs = gpu->perfcntrs;
	unsigned num_counters = gpu->perfcntr_groups[group_idx].num_counters;
	unsigned allocated_counters = perfcntrs->groups[group_idx]->allocated_counters;

	return num_counters - allocated_counters;
}

static void
__msm_perfcntr_cleanup(struct msm_gpu *gpu, struct msm_perfcntr_state *perfcntrs)
{
	struct device *dev = &gpu->pdev->dev;

	for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++)
		devm_kfree(dev, perfcntrs->groups[i]);

	devm_kfree(dev, perfcntrs);
}

void
msm_perfcntr_cleanup(struct msm_gpu *gpu)
{
	if (!gpu->perfcntrs)
		return;

	__msm_perfcntr_cleanup(gpu, gpu->perfcntrs);
	gpu->perfcntrs = NULL;
}

struct msm_perfcntr_state *
msm_perfcntr_init(struct msm_gpu *gpu)
{
	struct msm_perfcntr_state *perfcntrs;
	struct device *dev = &gpu->pdev->dev;
	size_t sz;

	sz = struct_size(perfcntrs, groups, gpu->num_perfcntr_groups);
	perfcntrs = devm_kzalloc(dev, sz, GFP_KERNEL);
	if (!perfcntrs)
		return ERR_PTR(-ENOMEM);

	for (unsigned i = 0; i < gpu->num_perfcntr_groups; i++) {
		const struct msm_perfcntr_group *group =
			&gpu->perfcntr_groups[i];

		sz = struct_size(perfcntrs->groups[i], countables, group->num_counters);
		perfcntrs->groups[i] = devm_kzalloc(dev, sz, GFP_KERNEL);
		if (!perfcntrs->groups[i]) {
			__msm_perfcntr_cleanup(gpu, perfcntrs);
			return ERR_PTR(-ENOMEM);
		}
	}

	return perfcntrs;
}
