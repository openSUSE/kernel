// SPDX-License-Identifier: GPL-2.0+
/*
 * Airoha Ethernet PHY common library
 *
 * Copyright (C) 2026 Airoha Technology Corp.
 * Copyright (C) 2026 Collabora Ltd.
 *                    Louis-Alexis Eyraud <louisalexis.eyraud@collabora.com>
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/phy.h>

#include "air_phy_lib.h"

#define AIR_EXT_PAGE_ACCESS		0x1f

int air_phy_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, AIR_EXT_PAGE_ACCESS);
}
EXPORT_SYMBOL_GPL(air_phy_read_page);

int air_phy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, AIR_EXT_PAGE_ACCESS, page);
}
EXPORT_SYMBOL_GPL(air_phy_write_page);

MODULE_DESCRIPTION("Airoha PHY Library");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Louis-Alexis Eyraud");
