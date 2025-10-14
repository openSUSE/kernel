/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_DRV_H__
#define __TIDSS_DRV_H__

#include <linux/spinlock.h>

#include <drm/drm_device.h>

#define TIDSS_MAX_PORTS 4
#define TIDSS_MAX_PLANES 4
#define TIDSS_MAX_OLDI_TXES 2

typedef u32 dispc_irq_t;
struct tidss_oldi;

struct tidss_device {
	struct drm_device ddev;		/* DRM device for DSS */
	struct device *dev;		/* Underlying DSS device */

	const struct dispc_features *feat;
	struct dispc_device *dispc;

	unsigned int num_crtcs;
	struct drm_crtc *crtcs[TIDSS_MAX_PORTS];

	unsigned int num_planes;
	struct drm_plane *planes[TIDSS_MAX_PLANES];

	unsigned int num_oldis;
	struct tidss_oldi *oldis[TIDSS_MAX_OLDI_TXES];

	unsigned int irq;

	/* protects the irq masks field and irqenable/irqstatus registers */
	spinlock_t irq_lock;
	dispc_irq_t irq_mask;	/* enabled irqs */
};

#define to_tidss(__dev) container_of(__dev, struct tidss_device, ddev)

int tidss_runtime_get(struct tidss_device *tidss);
void tidss_runtime_put(struct tidss_device *tidss);

#endif
