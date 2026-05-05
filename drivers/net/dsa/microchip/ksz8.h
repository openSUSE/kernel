/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Microchip KSZ8XXX series register access
 *
 * Copyright (C) 2020 Pengutronix, Michael Grzeschik <kernel@pengutronix.de>
 */

#ifndef __KSZ8XXX_H
#define __KSZ8XXX_H

#include <linux/types.h>
#include <net/dsa.h>
#include "ksz_common.h"

void ksz8_phylink_mac_link_up(struct phylink_config *config,
			      struct phy_device *phydev, unsigned int mode,
			      phy_interface_t interface, int speed, int duplex,
			      bool tx_pause, bool rx_pause);

extern const struct ksz_dev_ops ksz8463_dev_ops;
extern const struct ksz_dev_ops ksz87xx_dev_ops;
extern const struct ksz_dev_ops ksz88xx_dev_ops;

#endif
