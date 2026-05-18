// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/*
 * NXP NETC switch driver
 * Copyright 2025-2026 NXP
 */

#include "netc_switch.h"

struct netc_switch_platform {
	u16 revision;
	const struct netc_switch_info *info;
};

static const struct netc_switch_info imx94_info = {
	.num_ports = 4,
};

static const struct netc_switch_platform netc_platforms[] = {
	{ .revision = NETC_SWITCH_REV_4_3, .info = &imx94_info, },
	{ }
};

static const struct netc_switch_info *
netc_switch_get_info(struct netc_switch *priv)
{
	int i;

	/* Matching based on IP revision */
	for (i = 0; i < ARRAY_SIZE(netc_platforms); i++) {
		if (priv->revision == netc_platforms[i].revision)
			return netc_platforms[i].info;
	}

	return NULL;
}

int netc_switch_platform_probe(struct netc_switch *priv)
{
	const struct netc_switch_info *info = netc_switch_get_info(priv);

	if (!info) {
		dev_err(priv->dev, "Cannot find switch platform info\n");
		return -EINVAL;
	}

	priv->info = info;

	return 0;
}
