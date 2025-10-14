/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef __LINUX_IOMMU_PRIV_H
#define __LINUX_IOMMU_PRIV_H

#include <linux/iommu.h>
#include <linux/msi.h>

static inline const struct iommu_ops *dev_iommu_ops(struct device *dev)
{
	/*
	 * Assume that valid ops must be installed if iommu_probe_device()
	 * has succeeded. The device ops are essentially for internal use
	 * within the IOMMU subsystem itself, so we should be able to trust
	 * ourselves not to misuse the helper.
	 */
	return dev->iommu->iommu_dev->ops;
}

void dev_iommu_free(struct device *dev);

const struct iommu_ops *iommu_ops_from_fwnode(const struct fwnode_handle *fwnode);

static inline const struct iommu_ops *iommu_fwspec_ops(struct iommu_fwspec *fwspec)
{
	return iommu_ops_from_fwnode(fwspec ? fwspec->iommu_fwnode : NULL);
}

void iommu_fwspec_free(struct device *dev);

int iommu_device_register_bus(struct iommu_device *iommu,
			      const struct iommu_ops *ops,
			      const struct bus_type *bus,
			      struct notifier_block *nb);
void iommu_device_unregister_bus(struct iommu_device *iommu,
				 const struct bus_type *bus,
				 struct notifier_block *nb);

int iommu_mock_device_add(struct device *dev, struct iommu_device *iommu);

struct iommu_attach_handle *iommu_attach_handle_get(struct iommu_group *group,
						    ioasid_t pasid,
						    unsigned int type);
int iommu_attach_group_handle(struct iommu_domain *domain,
			      struct iommu_group *group,
			      struct iommu_attach_handle *handle);
void iommu_detach_group_handle(struct iommu_domain *domain,
			       struct iommu_group *group);
int iommu_replace_group_handle(struct iommu_group *group,
			       struct iommu_domain *new_domain,
			       struct iommu_attach_handle *handle);

#if IS_ENABLED(CONFIG_IOMMUFD_DRIVER_CORE) && IS_ENABLED(CONFIG_IRQ_MSI_IOMMU)
int iommufd_sw_msi(struct iommu_domain *domain, struct msi_desc *desc,
		   phys_addr_t msi_addr);
#else /* !CONFIG_IOMMUFD_DRIVER_CORE || !CONFIG_IRQ_MSI_IOMMU */
static inline int iommufd_sw_msi(struct iommu_domain *domain,
				 struct msi_desc *desc, phys_addr_t msi_addr)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_IOMMUFD_DRIVER_CORE && CONFIG_IRQ_MSI_IOMMU */

int iommu_replace_device_pasid(struct iommu_domain *domain,
			       struct device *dev, ioasid_t pasid,
			       struct iommu_attach_handle *handle);
#endif /* __LINUX_IOMMU_PRIV_H */
