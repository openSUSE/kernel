/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026 Airoha Technology Corp.
 * Copyright (C) 2026 Collabora Ltd.
 *                    Louis-Alexis Eyraud <louisalexis.eyraud@collabora.com>
 */

#ifndef __AIR_PHY_LIB_H
#define __AIR_PHY_LIB_H

#include <linux/phy.h>

int air_phy_read_page(struct phy_device *phydev);
int air_phy_write_page(struct phy_device *phydev, int page);

#endif /* __AIR_PHY_LIB_H */
