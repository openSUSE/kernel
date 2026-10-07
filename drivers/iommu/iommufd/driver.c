// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES
 */
#include "iommufd_private.h"

/* Driver should use a per-structure helper in include/linux/iommufd.h */
int _iommufd_object_depend(struct iommufd_object *obj_dependent,
			   struct iommufd_object *obj_depended)
{
	/* Reject self dependency that dead locks */
	if (obj_dependent == obj_depended)
		return -EINVAL;
	/* Only support dependency between two objects of the same type */
	if (obj_dependent->type != obj_depended->type)
		return -EINVAL;

	refcount_inc(&obj_depended->users);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(_iommufd_object_depend, IOMMUFD);

/* Driver should use a per-structure helper in include/linux/iommufd.h */
void _iommufd_object_undepend(struct iommufd_object *obj_dependent,
			      struct iommufd_object *obj_depended)
{
	if (WARN_ON_ONCE(obj_dependent == obj_depended ||
			 obj_dependent->type != obj_depended->type))
		return;

	refcount_dec(&obj_depended->users);
}
EXPORT_SYMBOL_NS_GPL(_iommufd_object_undepend, IOMMUFD);

/*
 * Allocate an @offset to return to user space to use for an mmap() syscall
 *
 * Driver should use a per-structure helper in include/linux/iommufd.h
 */
int _iommufd_alloc_mmap(struct iommufd_ctx *ictx, struct iommufd_object *owner,
			phys_addr_t mmio_addr, size_t length,
			unsigned long *offset)
{
	struct iommufd_mmap *immap;
	unsigned long startp;
	int rc;

	if (!PAGE_ALIGNED(mmio_addr))
		return -EINVAL;
	if (!length || !PAGE_ALIGNED(length))
		return -EINVAL;

	immap = kzalloc(sizeof(*immap), GFP_KERNEL);
	if (!immap)
		return -ENOMEM;
	immap->owner = owner;
	immap->length = length;
	immap->mmio_addr = mmio_addr;

	/* Skip the first page to ease caller identifying the returned offset */
	rc = mtree_alloc_range(&ictx->mt_mmap, &startp, immap, immap->length,
			       PAGE_SIZE, ULONG_MAX, GFP_KERNEL);
	if (rc < 0) {
		kfree(immap);
		return rc;
	}

	/* mmap() syscall will right-shift the offset in vma->vm_pgoff too */
	immap->vm_pgoff = startp >> PAGE_SHIFT;
	*offset = startp;
	return 0;
}
EXPORT_SYMBOL_NS_GPL(_iommufd_alloc_mmap, IOMMUFD);

/* Driver should use a per-structure helper in include/linux/iommufd.h */
void _iommufd_destroy_mmap(struct iommufd_ctx *ictx,
			   struct iommufd_object *owner, unsigned long offset)
{
	struct iommufd_mmap *immap;

	immap = mtree_erase(&ictx->mt_mmap, offset);
	WARN_ON_ONCE(!immap || immap->owner != owner);
	kfree(immap);
}
EXPORT_SYMBOL_NS_GPL(_iommufd_destroy_mmap, IOMMUFD);

/* Caller should xa_lock(&viommu->vdevs) to protect the return value */
struct device *iommufd_viommu_find_dev(struct iommufd_viommu *viommu,
				       unsigned long vdev_id)
{
	struct iommufd_vdevice *vdev;

	lockdep_assert_held(&viommu->vdevs.xa_lock);

	vdev = xa_load(&viommu->vdevs, vdev_id);
	return vdev ? vdev->dev : NULL;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_find_dev, IOMMUFD);

/* Return -ENOENT if device is not associated to the vIOMMU */
int iommufd_viommu_get_vdev_id(struct iommufd_viommu *viommu,
			       struct device *dev, unsigned long *vdev_id)
{
	struct iommufd_vdevice *vdev;
	unsigned long index;
	int rc = -ENOENT;

	if (WARN_ON_ONCE(!vdev_id))
		return -EINVAL;

	xa_lock(&viommu->vdevs);
	xa_for_each(&viommu->vdevs, index, vdev) {
		if (vdev->dev == dev) {
			*vdev_id = vdev->virt_id;
			rc = 0;
			break;
		}
	}
	xa_unlock(&viommu->vdevs);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_get_vdev_id, IOMMUFD);

/*
 * Typically called in driver's threaded IRQ handler.
 * The @type and @event_data must be defined in include/uapi/linux/iommufd.h
 */
int iommufd_viommu_report_event(struct iommufd_viommu *viommu,
				enum iommu_veventq_type type, void *event_data,
				size_t data_len)
{
	struct iommufd_veventq *veventq;
	struct iommufd_vevent *vevent;
	int rc = 0;

	if (WARN_ON_ONCE(!data_len || !event_data))
		return -EINVAL;

	down_read(&viommu->veventqs_rwsem);

	veventq = iommufd_viommu_find_veventq(viommu, type);
	if (!veventq) {
		rc = -EOPNOTSUPP;
		goto out_unlock_veventqs;
	}

	spin_lock(&veventq->common.lock);
	if (veventq->num_events == veventq->depth) {
		vevent = &veventq->lost_events_header;
		goto out_set_header;
	}

	vevent = kzalloc(struct_size(vevent, event_data, data_len), GFP_ATOMIC);
	if (!vevent) {
		rc = -ENOMEM;
		vevent = &veventq->lost_events_header;
		goto out_set_header;
	}
	vevent->data_len = data_len;
	memcpy(vevent->event_data, event_data, data_len);
	veventq->num_events++;

out_set_header:
	iommufd_vevent_handler(veventq, vevent);
	spin_unlock(&veventq->common.lock);
out_unlock_veventqs:
	up_read(&viommu->veventqs_rwsem);
	return rc;
}
EXPORT_SYMBOL_NS_GPL(iommufd_viommu_report_event, IOMMUFD);

MODULE_DESCRIPTION("iommufd code shared with builtin modules");
MODULE_LICENSE("GPL");
