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

struct pci_ecam_ops;

/**
 * struct pci_host_perst - PERST# GPIO descriptor
 * @list: List node for linking multiple PERST# GPIOs
 * @desc: GPIO descriptor for PERST# signal
 *
 * This structure holds a single PERST# GPIO descriptor.
 */
struct pci_host_perst {
	struct list_head	list;
	struct gpio_desc	*desc;
};

/**
 * struct pci_host_port - Generic Root Port properties
 * @list: List node for linking multiple ports
 * @perst: List of PERST# GPIO descriptors for this port and its children
 *
 * This structure contains common properties that can be parsed from
 * Root Port device tree nodes.
 */
struct pci_host_port {
	struct list_head	list;
	struct list_head	perst;
};

void pci_host_common_delete_ports(void *data);
int pci_host_common_parse_ports(struct device *dev,
				struct pci_host_bridge *bridge);
int pci_host_common_probe(struct platform_device *pdev);
int pci_host_common_init(struct platform_device *pdev,
			 struct pci_host_bridge *bridge,
			 const struct pci_ecam_ops *ops);
void pci_host_common_remove(struct platform_device *pdev);

struct pci_config_window *pci_host_common_ecam_create(struct device *dev,
	struct pci_host_bridge *bridge, const struct pci_ecam_ops *ops);

bool pci_host_common_d3cold_possible(struct pci_host_bridge *bridge,
				     bool *pme_capable);
#endif
