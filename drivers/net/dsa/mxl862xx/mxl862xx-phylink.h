/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_PHYLINK_H
#define __MXL862XX_PHYLINK_H

#include <linux/phylink.h>

#include "mxl862xx.h"

extern const struct phylink_mac_ops mxl862xx_phylink_mac_ops;
void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
			       struct phylink_config *config);

#endif /* __MXL862XX_PHYLINK_H */
