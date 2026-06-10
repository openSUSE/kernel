/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright 2025-2026 NXP */

#ifndef _PHY_FSL_LYNX_CORE_H
#define _PHY_FSL_LYNX_CORE_H

#include <linux/phy/phy.h>
#include <linux/phy.h>
#include <soc/fsl/phy-fsl-lynx.h>

#define LYNX_NUM_PLL				2

struct lynx_pccr {
	int offset;
	int width;
	int shift;
};

struct lynx_priv;

struct lynx_pll {
	struct lynx_priv *priv;
	int id;
	int refclk_sel;
	int frate_sel;
	bool enabled;
	bool locked;
	DECLARE_BITMAP(supported, LANE_MODE_MAX);
};

struct lynx_lane {
	struct lynx_priv *priv;
	struct phy *phy;
	bool powered_up;
	bool init;
	unsigned int id;
	enum lynx_lane_mode mode;
};

struct lynx_info {
	int (*get_pccr)(enum lynx_lane_mode lane_mode, int lane,
			struct lynx_pccr *pccr);
	int (*get_pcvt_offset)(int lane, enum lynx_lane_mode mode);
	bool (*lane_supports_mode)(int lane, enum lynx_lane_mode mode);
	int first_lane;
	int num_lanes;
};

struct lynx_priv {
	void __iomem *base;
	struct device *dev;
	const struct lynx_info *info;
	/* Serialize concurrent access to registers shared between lanes,
	 * like PCCn
	 */
	spinlock_t pcc_lock;
	struct lynx_pll pll[LYNX_NUM_PLL];
	struct lynx_lane *lane;

	struct delayed_work cdr_check;
};

static inline void lynx_rmw(struct lynx_priv *priv, unsigned long off, u32 val,
			    u32 mask)
{
	void __iomem *reg = priv->base + off;
	u32 orig, tmp;

	orig = ioread32(reg);
	tmp = orig & ~mask;
	tmp |= val;
	iowrite32(tmp, reg);
}

#define lynx_read(priv, off) \
	ioread32((priv)->base + (off))
#define lynx_write(priv, off, val) \
	iowrite32(val, (priv)->base + (off))
#define lynx_lane_rmw(lane, reg, val, mask)	\
	lynx_rmw((lane)->priv, reg(lane->id), val, mask)
#define lynx_lane_read(lane, reg)			\
	ioread32((lane)->priv->base + reg((lane)->id))
#define lynx_lane_write(lane, reg, val)		\
	iowrite32(val, (lane)->priv->base + reg((lane)->id))
#define lynx_pll_read(pll, reg)			\
	ioread32((pll)->priv->base + reg((pll)->id))

const char *lynx_lane_mode_str(enum lynx_lane_mode lane_mode);
enum lynx_lane_mode phy_interface_to_lane_mode(phy_interface_t intf);
bool lynx_lane_supports_mode(struct lynx_lane *lane, enum lynx_lane_mode mode);

struct lynx_pll *lynx_pll_get(struct lynx_priv *priv, enum lynx_lane_mode mode);

int lynx_pccr_read(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 *val);
int lynx_pccr_write(struct lynx_lane *lane, enum lynx_lane_mode mode, u32 val);
int lynx_pcvt_read(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		   u32 *val);
int lynx_pcvt_write(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		    u32 val);
int lynx_pcvt_rmw(struct lynx_lane *lane, enum lynx_lane_mode mode, int cr,
		  u32 val, u32 mask);

#endif
