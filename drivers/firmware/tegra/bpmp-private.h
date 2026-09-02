/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, NVIDIA CORPORATION.
 */

#ifndef __FIRMWARE_TEGRA_BPMP_PRIVATE_H
#define __FIRMWARE_TEGRA_BPMP_PRIVATE_H

#include <soc/tegra/bpmp.h>

struct tegra_bpmp_ops {
	int (*init)(struct tegra_bpmp *bpmp);
	void (*deinit)(struct tegra_bpmp *bpmp);
	bool (*is_response_ready)(struct tegra_bpmp_channel *channel);
	bool (*is_request_ready)(struct tegra_bpmp_channel *channel);
	int (*ack_response)(struct tegra_bpmp_channel *channel);
	int (*ack_request)(struct tegra_bpmp_channel *channel);
	bool (*is_response_channel_free)(struct tegra_bpmp_channel *channel);
	bool (*is_request_channel_free)(struct tegra_bpmp_channel *channel);
	int (*post_response)(struct tegra_bpmp_channel *channel);
	int (*post_request)(struct tegra_bpmp_channel *channel);
	int (*ring_doorbell)(struct tegra_bpmp *bpmp);
	int (*resume)(struct tegra_bpmp *bpmp);
};

extern const struct tegra_bpmp_ops tegra186_bpmp_ops;
extern const struct tegra_bpmp_ops tegra210_bpmp_ops;

#ifdef CONFIG_SYSFS
int tegra_bpmp_init_sysfs(struct tegra_bpmp *bpmp);
#else
static inline int tegra_bpmp_init_sysfs(struct tegra_bpmp *bpmp)
{
	return 0;
}
#endif

bool tegra_bpmp_mbwt_cmd_is_supported(struct tegra_bpmp *bpmp,
				      unsigned int cmd_code);
int tegra_bpmp_mbwt_get(struct tegra_bpmp *bpmp, unsigned int instance,
			unsigned int vc_type, unsigned int *bandwidth);
int tegra_bpmp_mbwt_set(struct tegra_bpmp *bpmp, unsigned int instance,
			unsigned int vc_type, unsigned int bandwidth);

#endif
