/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx Zynq MPSoC CSU Register Access
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 *  Michal Simek <michal.simek@amd.com>
 *  Ronak Jain <ronak.jain@amd.com>
 */

#ifndef __ZYNQMP_CSU_REG_H__
#define __ZYNQMP_CSU_REG_H__

#include <linux/platform_device.h>

int zynqmp_csu_discover_registers(struct platform_device *pdev);

#endif /* __ZYNQMP_CSU_REG_H__ */
