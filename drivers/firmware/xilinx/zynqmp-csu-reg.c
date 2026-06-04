// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC CSU Register Access
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 *  Michal Simek <michal.simek@amd.com>
 *  Ronak Jain <ronak.jain@amd.com>
 */

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "zynqmp-csu-reg.h"

/* Node ID for CSU module in firmware */
#define CSU_NODE_ID 0

/* Maximum number of CSU registers supported */
#define MAX_CSU_REGS 50

/* Size of register name returned by firmware (3 u32 words = 12 bytes) */
#define CSU_REG_NAME_LEN 12

/**
 * struct zynqmp_csu_reg - CSU register information
 * @id: Register index from firmware
 * @name: Register name
 * @attr: Device attribute for sysfs
 */
struct zynqmp_csu_reg {
	u32 id;
	char name[CSU_REG_NAME_LEN];
	struct device_attribute attr;
};

/**
 * struct zynqmp_csu_data - Per-device CSU data
 * @csu_regs: Array of CSU registers
 * @csu_attr_group: Attribute group for sysfs
 */
struct zynqmp_csu_data {
	struct zynqmp_csu_reg *csu_regs;
	struct attribute_group csu_attr_group;
};

/**
 * zynqmp_pm_get_node_count() - Get number of supported nodes via QUERY_DATA
 *
 * Return: Number of nodes on success, or negative error code
 */
static int zynqmp_pm_get_node_count(void)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_GET_NODE_COUNT;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	if (ret)
		return ret;

	return ret_payload[1];
}

/**
 * zynqmp_pm_get_node_name() - Get node name via QUERY_DATA
 * @index: Register index
 * @name: Buffer to store register name
 *
 * Return: 0 on success, error code otherwise
 */
static int zynqmp_pm_get_node_name(u32 index, char *name)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_GET_NODE_NAME;
	qdata.arg1 = index;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	if (ret)
		return ret;

	memcpy(name, &ret_payload[1], CSU_REG_NAME_LEN);
	name[CSU_REG_NAME_LEN - 1] = '\0';

	return 0;
}

/**
 * zynqmp_csu_reg_show() - Generic show function for all registers
 * @dev: Device pointer
 * @attr: Device attribute
 * @buf: Output buffer
 *
 * Return: Number of bytes written to buffer, or error code
 */
static ssize_t zynqmp_csu_reg_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct zynqmp_csu_reg *reg;
	u32 value;
	int ret;

	/* Use container_of to get register directly */
	reg = container_of(attr, struct zynqmp_csu_reg, attr);

	ret = zynqmp_pm_sec_read_reg(CSU_NODE_ID, reg->id, &value);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%08x\n", value);
}

/**
 * zynqmp_csu_reg_store() - Generic store function for writable registers
 * @dev: Device pointer
 * @attr: Device attribute
 * @buf: Input buffer
 * @count: Buffer size
 *
 * Format: "mask value" - both mask and value required
 * Example: echo "0xFFFFFFFF 0x12345678" > register
 *
 * Return: count on success, error code otherwise
 */
static ssize_t zynqmp_csu_reg_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct zynqmp_csu_reg *reg;
	u32 mask, value;
	int ret;

	reg = container_of(attr, struct zynqmp_csu_reg, attr);

	if (sscanf(buf, "%x %x", &mask, &value) != 2)
		return -EINVAL;

	ret = zynqmp_pm_sec_mask_write_reg(CSU_NODE_ID, reg->id, mask, value);
	if (ret)
		return ret;

	return count;
}

/**
 * zynqmp_csu_discover_registers() - Discover CSU registers from firmware
 * @pdev: Platform device pointer
 *
 * This function uses PM_QUERY_DATA to discover all available CSU registers
 * and creates sysfs group under /sys/devices/platform/firmware:zynqmp-firmware/
 *
 * Return: 0 on success, error code otherwise
 */
int zynqmp_csu_discover_registers(struct platform_device *pdev)
{
	struct zynqmp_csu_data *csu_data;
	struct attribute **attrs;
	int count, ret, i;

	ret = zynqmp_pm_is_function_supported(PM_QUERY_DATA, PM_QID_GET_NODE_COUNT);
	if (ret) {
		dev_dbg(&pdev->dev, "CSU register discovery not supported by current firmware\n");
		return 0;
	}

	ret = zynqmp_pm_is_function_supported(PM_QUERY_DATA, PM_QID_GET_NODE_NAME);
	if (ret) {
		dev_dbg(&pdev->dev, "CSU register name query not supported by current firmware\n");
		return 0;
	}

	count = zynqmp_pm_get_node_count();
	if (count < 0)
		return count;
	if (count == 0) {
		dev_dbg(&pdev->dev, "No nodes available from firmware\n");
		return 0;
	}

	/* Validate count to prevent excessive memory allocation */
	if (count > MAX_CSU_REGS) {
		dev_err(&pdev->dev, "Register count %d exceeds maximum %d\n",
			count, MAX_CSU_REGS);
		return -EINVAL;
	}

	dev_dbg(&pdev->dev, "Discovered %d nodes from firmware\n", count);

	csu_data = devm_kzalloc(&pdev->dev, sizeof(*csu_data), GFP_KERNEL);
	if (!csu_data)
		return -ENOMEM;

	csu_data->csu_regs = devm_kcalloc(&pdev->dev, count, sizeof(*csu_data->csu_regs),
					  GFP_KERNEL);
	if (!csu_data->csu_regs) {
		devm_kfree(&pdev->dev, csu_data);
		return -ENOMEM;
	}

	attrs = devm_kcalloc(&pdev->dev, count + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs) {
		devm_kfree(&pdev->dev, csu_data->csu_regs);
		devm_kfree(&pdev->dev, csu_data);
		return -ENOMEM;
	}

	for (i = 0; i < count; i++) {
		struct zynqmp_csu_reg *reg = &csu_data->csu_regs[i];
		struct device_attribute *dev_attr = &reg->attr;

		reg->id = i;

		ret = zynqmp_pm_get_node_name(i, reg->name);
		if (ret) {
			dev_warn(&pdev->dev, "Failed to get name for register %d\n", i);
			snprintf(reg->name, sizeof(reg->name), "csu_reg_%d", i);
		}

		/*
		 * The firmware does not expose per-register access mode via
		 * PM_QUERY_DATA today, so the kernel cannot tell read-only
		 * registers from read-write ones at discovery time. Expose
		 * every register as 0644 and rely on the firmware to reject
		 * IOCTL_MASK_WRITE_REG on read-only registers; the error is
		 * propagated back to userspace from the store callback.
		 */
		sysfs_attr_init(&dev_attr->attr);
		dev_attr->attr.name = reg->name;
		dev_attr->attr.mode = 0644;
		dev_attr->show = zynqmp_csu_reg_show;
		dev_attr->store = zynqmp_csu_reg_store;

		attrs[i] = &dev_attr->attr;

		dev_dbg(&pdev->dev, "Register %d: id=%d name=%s\n", i, reg->id, reg->name);
	}

	csu_data->csu_attr_group.name = "csu_registers";
	csu_data->csu_attr_group.attrs = attrs;

	ret = devm_device_add_group(&pdev->dev, &csu_data->csu_attr_group);
	if (ret) {
		devm_kfree(&pdev->dev, attrs);
		devm_kfree(&pdev->dev, csu_data->csu_regs);
		devm_kfree(&pdev->dev, csu_data);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(zynqmp_csu_discover_registers);
