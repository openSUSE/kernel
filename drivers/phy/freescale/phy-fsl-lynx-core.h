/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright 2025-2026 NXP */

#ifndef _PHY_FSL_LYNX_CORE_H
#define _PHY_FSL_LYNX_CORE_H

#include <linux/phy.h>
#include <soc/fsl/phy-fsl-lynx.h>

struct lynx_pccr {
	int offset;
	int width;
	int shift;
};

struct lynx_info {
	bool (*lane_supports_mode)(int lane, enum lynx_lane_mode mode);
	int first_lane;
};

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode);
enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf);

#endif
