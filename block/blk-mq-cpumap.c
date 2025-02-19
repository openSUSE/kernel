// SPDX-License-Identifier: GPL-2.0
/*
 * CPU <-> hardware queue mapping helpers
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/group_cpus.h>
#include <linux/device/bus.h>
#include <linux/sched/isolation.h>

#include "blk.h"
#include "blk-mq.h"

static unsigned int blk_mq_num_queues(const struct cpumask *mask,
				      unsigned int max_queues)
{
	unsigned int num;

	if (housekeeping_enabled(HK_TYPE_MANAGED_IRQ))
		mask = housekeeping_cpumask(HK_TYPE_MANAGED_IRQ);

	num = cpumask_weight(mask);
	return min_not_zero(num, max_queues);
}

/**
 * blk_mq_num_possible_queues - Calc nr of queues for multiqueue devices
 * @max_queues:	The maximal number of queues the hardware/driver
 *		supports. If max_queues is 0, the argument is
 *		ignored.
 *
 * Calculate the number of queues which should be used for a multiqueue
 * device based on the number of possible cpu. The helper is considering
 * isolcpus settings.
 */
unsigned int blk_mq_num_possible_queues(unsigned int max_queues)
{
	return blk_mq_num_queues(cpu_possible_mask, max_queues);
}
EXPORT_SYMBOL_GPL(blk_mq_num_possible_queues);

/**
 * blk_mq_num_online_queues - Calc nr of queues for multiqueue devices
 * @max_queues:	The maximal number of queues the hardware/driver
 *		supports. If max_queues is 0, the argument is
 *		ignored.
 *
 * Calculate the number of queues which should be used for a multiqueue
 * device based on the number of online cpus. The helper is considering
 * isolcpus settings.
 */
unsigned int blk_mq_num_online_queues(unsigned int max_queues)
{
	return blk_mq_num_queues(cpu_online_mask, max_queues);
}
EXPORT_SYMBOL_GPL(blk_mq_num_online_queues);

/*
 * blk_mq_map_hk_queues - Create housekeeping CPU to hardware queue mapping
 * @qmap:	CPU to hardware queue map
 *
 * Create a housekeeping CPU to hardware queue mapping in @qmap. If the
 * isolcpus feature is enabled and blk_mq_map_hk_queues returns true,
 * @qmap contains a valid configuration honoring the managed_irq
 * configuration. If the isolcpus feature is disabled this function
 * returns false.
 */
static bool blk_mq_map_hk_queues(struct blk_mq_queue_map *qmap)
{
	struct cpumask *hk_masks;
	cpumask_var_t isol_mask;
	unsigned int queue, cpu, nr_masks;

	if (!housekeeping_enabled(HK_TYPE_MANAGED_IRQ))
		return false;

	/* map housekeeping cpus to matching hardware context */
	hk_masks = group_cpus_evenly_nm(qmap->nr_queues, &nr_masks);
	if (!hk_masks)
		goto fallback;

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		for_each_cpu(cpu, &hk_masks[queue % nr_masks])
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}

	kfree(hk_masks);

	/* map isolcpus to hardware context */
	if (!alloc_cpumask_var(&isol_mask, GFP_KERNEL))
		goto fallback;

	queue = 0;
	cpumask_andnot(isol_mask,
		       cpu_possible_mask,
		       housekeeping_cpumask(HK_TYPE_MANAGED_IRQ));

	for_each_cpu(cpu, isol_mask) {
		qmap->mq_map[cpu] = qmap->queue_offset + queue;
		queue = (queue + 1) % qmap->nr_queues;
	}

	free_cpumask_var(isol_mask);

	return true;

fallback:
	/* map all cpus to hardware context ignoring any affinity */
	queue = 0;
	for_each_possible_cpu(cpu) {
		qmap->mq_map[cpu] = qmap->queue_offset + queue;
		queue = (queue + 1) % qmap->nr_queues;
	}
	return true;
}

void blk_mq_map_queues(struct blk_mq_queue_map *qmap)
{
	const struct cpumask *masks;
	unsigned int queue, cpu, nr_masks;

	if (blk_mq_map_hk_queues(qmap))
		return;

	masks = group_cpus_evenly_nm(qmap->nr_queues, &nr_masks);
	if (!masks) {
		for_each_possible_cpu(cpu)
			qmap->mq_map[cpu] = qmap->queue_offset;
		return;
	}

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		for_each_cpu(cpu, &masks[queue % nr_masks])
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}
	kfree(masks);
}
EXPORT_SYMBOL_GPL(blk_mq_map_queues);

/**
 * blk_mq_hw_queue_to_node - Look up the memory node for a hardware queue index
 * @qmap: CPU to hardware queue map.
 * @index: hardware queue index.
 *
 * We have no quick way of doing reverse lookups. This is only used at
 * queue init time, so runtime isn't important.
 */
int blk_mq_hw_queue_to_node(struct blk_mq_queue_map *qmap, unsigned int index)
{
	int i;

	for_each_possible_cpu(i) {
		if (index == qmap->mq_map[i])
			return cpu_to_node(i);
	}

	return NUMA_NO_NODE;
}

/**
 * blk_mq_map_hw_queues - Create CPU to hardware queue mapping
 * @qmap:	CPU to hardware queue map
 * @dev:	The device to map queues
 * @offset:	Queue offset to use for the device
 *
 * Create a CPU to hardware queue mapping in @qmap. The struct bus_type
 * irq_get_affinity callback will be used to retrieve the affinity.
 */
void blk_mq_map_hw_queues(struct blk_mq_queue_map *qmap,
			  struct device *dev, unsigned int offset)

{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	if (!dev->bus->irq_get_affinity)
		goto fallback;

	if (blk_mq_map_hk_queues(qmap))
		return;

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		mask = dev->bus->irq_get_affinity(dev, queue + offset);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}

	return;

fallback:
	blk_mq_map_queues(qmap);
}
EXPORT_SYMBOL_GPL(blk_mq_map_hw_queues);
