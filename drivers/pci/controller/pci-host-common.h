/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common library for PCI host controller drivers
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#ifndef _PCI_HOST_COMMON_H
#define _PCI_HOST_COMMON_H

#include <linux/delay.h>
#include "../pci.h"

struct pci_ecam_ops;

int pci_host_common_probe(struct platform_device *pdev);
int pci_host_common_init(struct platform_device *pdev,
			 struct pci_host_bridge *bridge,
			 const struct pci_ecam_ops *ops);
void pci_host_common_remove(struct platform_device *pdev);

struct pci_config_window *pci_host_common_ecam_create(struct device *dev,
	struct pci_host_bridge *bridge, const struct pci_ecam_ops *ops);

/**
 * pci_host_common_link_train_delay - Wait 100 ms if link speed > 5 GT/s
 * @max_link_speed: the maximum link speed (2 = 5.0 GT/s, 3 = 8.0 GT/s, ...)
 *
 * Must be called after Link training completes and before the first
 * Configuration Request is sent.
 */
static inline void pci_host_common_link_train_delay(int max_link_speed)
{
	if (max_link_speed > 2)
		msleep(PCIE_RESET_CONFIG_WAIT_MS);
}

#endif
