/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Arm Limited
 */
#ifndef __LINUX_ARM_SMCCC_BUS_H
#define __LINUX_ARM_SMCCC_BUS_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

struct arm_smccc_device {
	int id;
	char name[ARM_SMCCC_NAME_SIZE];
	struct device dev;
};

#define to_arm_smccc_device(d) container_of(d, struct arm_smccc_device, dev)

struct arm_smccc_driver {
	const char *name;
	int (*probe)(struct arm_smccc_device *sdev);
	void (*remove)(struct arm_smccc_device *sdev);
	const struct arm_smccc_device_id *id_table;

	struct device_driver driver;
};

#define to_arm_smccc_driver(d) \
	container_of_const(d, struct arm_smccc_driver, driver)

int arm_smccc_driver_register(struct arm_smccc_driver *driver,
		struct module *owner, const char *mod_name);
void arm_smccc_driver_unregister(struct arm_smccc_driver *driver);
struct arm_smccc_device *arm_smccc_device_register(const char *name);
void arm_smccc_device_unregister(struct arm_smccc_device *smcc_dev);

#define arm_smccc_register(driver) \
	arm_smccc_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#define arm_smccc_unregister(driver) \
	arm_smccc_driver_unregister(driver)

#define module_arm_smccc_driver(__arm_smccc_driver) \
	module_driver(__arm_smccc_driver, arm_smccc_register, \
		      arm_smccc_unregister)

extern const struct bus_type arm_smccc_bus_type;

#endif /* __LINUX_ARM_SMCCC_BUS_H */
