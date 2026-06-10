// SPDX-License-Identifier: GPL-2.0+
/* Copyright 2025-2026 NXP */

#include <linux/module.h>

#include "phy-fsl-lynx-core.h"

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode)
{
	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		return "1000Base-X/SGMII";
	case LANE_MODE_10GBASER:
		return "10GBase-R";
	case LANE_MODE_USXGMII:
		return "USXGMII";
	case LANE_MODE_25GBASER:
		return "25GBase-R";
	default:
		return "unknown";
	}
}
EXPORT_SYMBOL_NS_GPL(lynx_lane_mode_str, "PHY_FSL_LYNX");

enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf)
{
	switch (intf) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		return LANE_MODE_1000BASEX_SGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return LANE_MODE_10GBASER;
	case PHY_INTERFACE_MODE_USXGMII:
		return LANE_MODE_USXGMII;
	case PHY_INTERFACE_MODE_25GBASER:
		return LANE_MODE_25GBASER;
	default:
		return LANE_MODE_UNKNOWN;
	}
}
EXPORT_SYMBOL_NS_GPL(phy_interface_to_lane_mode, "PHY_FSL_LYNX");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Freescale Lynx SerDes core functionality");
