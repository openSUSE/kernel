/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2025-2026 NXP */
#ifndef __NETC_NTMP_H
#define __NETC_NTMP_H

#include <linux/bitops.h>
#include <linux/if_ether.h>

#define NTMP_NULL_ENTRY_ID		0xffffffffU

struct maft_keye_data {
	u8 mac_addr[ETH_ALEN];
	__le16 resv;
};

struct maft_cfge_data {
	__le16 si_bitmap;
	__le16 resv;
};

struct netc_cbdr_regs {
	void __iomem *pir;
	void __iomem *cir;
	void __iomem *mr;

	void __iomem *bar0;
	void __iomem *bar1;
	void __iomem *lenr;
};

struct netc_tbl_vers {
	u8 maft_ver;
	u8 rsst_ver;
	u8 fdbt_ver;
	u8 vft_ver;
};

struct netc_swcbd {
	void *buf;
	dma_addr_t dma;
	size_t size;
};

struct netc_cbdr {
	struct device *dev;
	struct netc_cbdr_regs regs;

	int bd_num;
	int next_to_use;
	int next_to_clean;

	int dma_size;
	void *addr_base;
	void *addr_base_align;
	dma_addr_t dma_base;
	dma_addr_t dma_base_align;
	struct netc_swcbd *swcbd;

	/* Serialize the order of command BD ring */
	struct mutex ring_lock;
};

struct ntmp_user {
	int cbdr_num;	/* number of control BD ring */
	struct device *dev;
	struct netc_cbdr *ring;
	struct netc_tbl_vers tbl;
};

struct maft_entry_data {
	struct maft_keye_data keye;
	struct maft_cfge_data cfge;
};

struct fdbt_keye_data {
	u8 mac_addr[ETH_ALEN]; /* big-endian */
	__le16 resv0;
	__le16 fid;
#define FDBT_FID		GENMASK(11, 0)
	__le16 resv1;
};

struct fdbt_cfge_data {
	__le32 port_bitmap;
#define FDBT_PORT_BITMAP	GENMASK(23, 0)
	__le32 cfg;
#define FDBT_OETEID		GENMASK(1, 0)
#define FDBT_EPORT		GENMASK(6, 2)
#define FDBT_IMIRE		BIT(7)
#define FDBT_CTD		GENMASK(10, 9)
#define FDBT_DYNAMIC		BIT(11)
#define FDBT_TIMECAPE		BIT(12)
	__le32 et_eid;
};

struct fdbt_entry_data {
	u32 entry_id;
	struct fdbt_keye_data keye;
	struct fdbt_cfge_data cfge;
	u8 acte;
#define FDBT_ACT_CNT		GENMASK(6, 0)
#define FDBT_ACT_FLAG		BIT(7)
};

struct vft_cfge_data {
	__le32 bitmap_stg;
#define VFT_PORT_MEMBERSHIP	GENMASK(23, 0)
#define VFT_STG_ID_MASK		GENMASK(27, 24)
#define VFT_STG_ID(g)		FIELD_PREP(VFT_STG_ID_MASK, (g))
	__le16 fid;
#define VFT_FID			GENMASK(11, 0)
	__le16 cfg;
#define VFT_MLO			GENMASK(2, 0)
#define VFT_MFO			GENMASK(4, 3)
#define VFT_IPMFE		BIT(6)
#define VFT_IPMFLE		BIT(7)
#define VFT_PGA			BIT(8)
#define VFT_SFDA		BIT(10)
#define VFT_OSFDA		BIT(11)
#define VFT_FDBAFSS		BIT(12)
	__le32 eta_port_bitmap;
#define VFT_ETA_PORT_BITMAP	GENMASK(23, 0)
	__le32 et_eid;
};

#if IS_ENABLED(CONFIG_NXP_NETC_LIB)
int ntmp_init_cbdr(struct netc_cbdr *cbdr, struct device *dev,
		   const struct netc_cbdr_regs *regs);
void ntmp_free_cbdr(struct netc_cbdr *cbdr);

/* NTMP APIs */
int ntmp_maft_add_entry(struct ntmp_user *user, u32 entry_id,
			struct maft_entry_data *maft);
int ntmp_maft_query_entry(struct ntmp_user *user, u32 entry_id,
			  struct maft_entry_data *maft);
int ntmp_maft_delete_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_rsst_update_entry(struct ntmp_user *user, const u32 *table,
			   int count);
int ntmp_rsst_query_entry(struct ntmp_user *user,
			  u32 *table, int count);
int ntmp_fdbt_add_entry(struct ntmp_user *user, u32 *entry_id,
			const struct fdbt_keye_data *keye,
			const struct fdbt_cfge_data *cfge);
int ntmp_fdbt_update_entry(struct ntmp_user *user, u32 entry_id,
			   const struct fdbt_cfge_data *cfge);
int ntmp_fdbt_delete_entry(struct ntmp_user *user, u32 entry_id);
int ntmp_fdbt_search_port_entry(struct ntmp_user *user, int port,
				u32 *resume_entry_id,
				struct fdbt_entry_data *entry);
int ntmp_vft_add_entry(struct ntmp_user *user, u16 vid,
		       const struct vft_cfge_data *cfge);
#else
static inline int ntmp_init_cbdr(struct netc_cbdr *cbdr, struct device *dev,
				 const struct netc_cbdr_regs *regs)
{
	return 0;
}

static inline void ntmp_free_cbdr(struct netc_cbdr *cbdr)
{
}

static inline int ntmp_maft_add_entry(struct ntmp_user *user, u32 entry_id,
				      struct maft_entry_data *maft)
{
	return 0;
}

static inline int ntmp_maft_query_entry(struct ntmp_user *user, u32 entry_id,
					struct maft_entry_data *maft)
{
	return 0;
}

static inline int ntmp_maft_delete_entry(struct ntmp_user *user, u32 entry_id)
{
	return 0;
}

static inline int ntmp_rsst_update_entry(struct ntmp_user *user,
					 const u32 *table, int count)
{
	return 0;
}

static inline int ntmp_rsst_query_entry(struct ntmp_user *user,
					u32 *table, int count)
{
	return 0;
}

#endif

#endif
