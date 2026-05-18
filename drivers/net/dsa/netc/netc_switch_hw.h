/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2025-2026 NXP
 */

#ifndef _NETC_SWITCH_HW_H
#define _NETC_SWITCH_HW_H

#include <linux/bitops.h>

#define NETC_SWITCH_VENDOR_ID		0x1131
#define NETC_SWITCH_DEVICE_ID		0xeef2

/* Definition of Switch base registers */
#define NETC_BPCAPR			0x0008
#define  BPCAPR_NUM_BP			GENMASK(7, 0)

#define NETC_PBPMCR0			0x0400
#define NETC_PBPMCR1			0x0404

#define NETC_CBDRMR(a)			(0x0800 + (a) * 0x30)
#define NETC_CBDRBAR0(a)		(0x0810 + (a) * 0x30)
#define NETC_CBDRBAR1(a)		(0x0814 + (a) * 0x30)
#define NETC_CBDRPIR(a)			(0x0818 + (a) * 0x30)
#define NETC_CBDRCIR(a)			(0x081c + (a) * 0x30)
#define NETC_CBDRLENR(a)		(0x0820 + (a) * 0x30)

#define NETC_SWCR			0x1018
#define  SWCR_SWID			GENMASK(2, 0)

#define NETC_DOSL2CR			0x1220
#define  DOSL2CR_SAMEADDR		BIT(0)
#define  DOSL2CR_MSAMCC			BIT(1)

#define NETC_DOSL3CR			0x1224
#define  DOSL3CR_SAMEADDR		BIT(0)
#define  DOSL3CR_IPSAMCC		BIT(1)

/* Hash table memory capability register, the memory is shared by
 * the following tables:
 *
 * - Ingress Stream Identification table
 * - Ingress Stream Filter table
 * - VLAN Filter table
 * - FDB table
 * - L2 IPv4 Multicast Filter table
 *
 *  Each hash table entry is one word in size.
 */
#define NETC_HTMCAPR			0x1900
#define  HTMCAPR_NUM_WORDS		GENMASK(15, 0)

#define NETC_VFHTDECR1			0x2014
#define NETC_VFHTDECR2			0x2018
#define  VFHTDECR2_ET_PORT(a)		BIT((a))
#define  VFHTDECR2_MLO			GENMASK(26, 24)
#define  VFHTDECR2_MFO			GENMASK(28, 27)

/* Definition of Switch port registers */
#define NETC_PCAPR			0x0000
#define  PCAPR_LINK_TYPE		BIT(4)
#define  PCAPR_NUM_TC			GENMASK(15, 12)
#define  PCAPR_NUM_Q			GENMASK(19, 16)
#define  PCAPR_NUM_CG			GENMASK(27, 24)
#define  PCAPR_TGS			BIT(28)
#define  PCAPR_CBS			BIT(29)

#define NETC_PMCAPR			0x0004
#define  PMCAPR_HD			BIT(8)
#define  PMCAPR_FP			GENMASK(10, 9)
#define   FP_SUPPORT			2

#define NETC_PCR			0x0010
#define  PCR_HDR_FMT			BIT(0)
#define  PCR_NS_TAG_PORT		BIT(3)
#define  PCR_L2DOSE			BIT(4)
#define  PCR_L3DOSE			BIT(5)
#define  PCR_TIMER_CS			BIT(8)
#define  PCR_PSPEED			GENMASK(29, 16)
#define   PSPEED_SET_VAL(s)		FIELD_PREP(PCR_PSPEED, ((s) / 10 - 1))

#define NETC_PQOSMR			0x0054
#define  PQOSMR_VS			BIT(0)
#define  PQOSMR_VE			BIT(1)
#define  PQOSMR_DDR			GENMASK(3, 2)
#define  PQOSMR_DIPV			GENMASK(6, 4)
#define  PQOSMR_VQMP			GENMASK(19, 16)
#define  PQOSMR_QVMP			GENMASK(23, 20)

#define NETC_PIPFCR			0x0084
#define  PIPFCR_EN			BIT(0)

#define NETC_POR			0x100
#define  POR_TXDIS			BIT(0)
#define  POR_RXDIS			BIT(1)

#define NETC_PSR			0x104
#define  PSR_TX_BUSY			BIT(0)
#define  PSR_RX_BUSY			BIT(1)

#define NETC_PTCTMSDUR(a)		(0x208 + (a) * 0x20)
#define  PTCTMSDUR_MAXSDU		GENMASK(15, 0)
#define  PTCTMSDUR_SDU_TYPE		GENMASK(17, 16)
#define   SDU_TYPE_PPDU			0
#define   SDU_TYPE_MPDU			1
#define   SDU_TYPE_MSDU			2

#define NETC_BPCR			0x500
#define  BPCR_DYN_LIMIT			GENMASK(15, 0)
#define  BPCR_MLO			GENMASK(22, 20)
#define  BPCR_UUCASTE			BIT(24)
#define  BPCR_UMCASTE			BIT(25)
#define  BPCR_MCASTE			BIT(26)
#define  BPCR_BCASTE			BIT(27)
#define  BPCR_STAMVD			BIT(28)
#define  BPCR_SRCPRND			BIT(29)

/* MAC learning options, see BPCR[MLO], VFHTDECR2[MLO] and
 * VLAN Filter Table CFGE_DATA[MLO]
 */
enum netc_mlo {
	MLO_NOT_OVERRIDE = 0,
	MLO_DISABLE,
	MLO_HW,
	MLO_SW_SEC,
	MLO_SW_UNSEC,
	MLO_DISABLE_SMAC,
};

/* MAC forwarding options, see VFHTDECR2[MFO] and VLAN
 * Filter Table CFGE_DATA[MFO]
 */
enum netc_mfo {
	MFO_NO_FDB_LOOKUP = 1,
	MFO_NO_MATCH_FLOOD,
	MFO_NO_MATCH_DISCARD,
};

#define NETC_BPDVR			0x510
#define  BPDVR_VID			GENMASK(11, 0)
#define  BPDVR_DEI			BIT(12)
#define  BPDVR_PCP			GENMASK(15, 13)
#define  BPDVR_TPID			BIT(16)
#define  BPDVR_RXTAGA			GENMASK(23, 20)
#define  BPDVR_RXVAM			BIT(24)
#define  BPDVR_TXTAGA			GENMASK(26, 25)

#define NETC_BPSTGSR			0x520

enum netc_stg_stage {
	NETC_STG_STATE_DISABLED = 0,
	NETC_STG_STATE_LEARNING,
	NETC_STG_STATE_FORWARDING,
};

/* Definition of Switch ethernet MAC port registers */
#define NETC_PMAC_OFFSET		0x400
#define NETC_PM_CMD_CFG(a)		(0x1008 + (a) * 0x400)
#define  PM_CMD_CFG_TX_EN		BIT(0)
#define  PM_CMD_CFG_RX_EN		BIT(1)
#define  PM_CMD_CFG_PAUSE_IGN		BIT(8)

#define NETC_PM_MAXFRM(a)		(0x1014 + (a) * 0x400)
#define  PM_MAXFRAM			GENMASK(15, 0)

#define NETC_PM_IEVENT(a)		(0x1040 + (a) * 0x400)
#define  PM_IEVENT_TX_EMPTY		BIT(5)
#define  PM_IEVENT_RX_EMPTY		BIT(6)

#define NETC_PM_PAUSE_QUANTA(a)		(0x1054 + (a) * 0x400)
#define NETC_PM_PAUSE_THRESH(a)		(0x1064 + (a) * 0x400)

#define NETC_PM_IF_MODE(a)		(0x1300 + (a) * 0x400)
#define  PM_IF_MODE_IFMODE		GENMASK(2, 0)
#define   IFMODE_MII			1
#define   IFMODE_RMII			3
#define   IFMODE_RGMII			4
#define   IFMODE_SGMII			5
#define  PM_IF_MODE_REVMII		BIT(3)
#define  PM_IF_MODE_M10			BIT(4)
#define  PM_IF_MODE_HD			BIT(6)
#define  PM_IF_MODE_SSP			GENMASK(14, 13)
#define   SSP_100M			0
#define   SSP_10M			1
#define   SSP_1G			2

#define NETC_PEMDIOCR			0x1c00
#define NETC_EMDIO_BASE			NETC_PEMDIOCR

/* Definition of global registers (read only) */
#define NETC_IPBRR0			0x0bf8
#define  IPBRR0_IP_REV			GENMASK(15, 0)

#endif
