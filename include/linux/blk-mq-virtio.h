/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BLK_MQ_VIRTIO_H
#define _LINUX_BLK_MQ_VIRTIO_H

struct blk_mq_queue_map;
struct virtio_device;

void blk_mq_virtio_map_queues(struct blk_mq_queue_map *qmap,
		struct virtio_device *vdev, int first_vec);
const struct cpumask *blk_mq_virtio_get_queue_affinity(void *dev_data,
						       int offset,
						       int queue);

#endif /* _LINUX_BLK_MQ_VIRTIO_H */
