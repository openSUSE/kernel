/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 */

#ifndef UFS_QCOM_H_
#define UFS_QCOM_H_

#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <soc/qcom/ice.h>
#include <ufs/ufshcd.h>

#define MPHY_TX_FSM_STATE       0x41
#define TX_FSM_HIBERN8          0x1
#define HBRN8_POLL_TOUT_MS      100
#define DEFAULT_CLK_RATE_HZ     1000000
#define MAX_SUPP_MAC		64
#define MAX_ESI_VEC		32

#define UFS_HW_VER_MAJOR_MASK	GENMASK(31, 28)
#define UFS_HW_VER_MINOR_MASK	GENMASK(27, 16)
#define UFS_HW_VER_STEP_MASK	GENMASK(15, 0)
#define UFS_DEV_VER_MAJOR_MASK	GENMASK(7, 4)

#define UFS_QCOM_LIMIT_HS_RATE		PA_HS_MODE_B

/* bit and mask definitions for PA_VS_CLK_CFG_REG attribute */
#define PA_VS_CLK_CFG_REG      0x9004
#define PA_VS_CLK_CFG_REG_MASK GENMASK(8, 0)

/* bit and mask definitions for DL_VS_CLK_CFG attribute */
#define DL_VS_CLK_CFG          0xA00B
#define DL_VS_CLK_CFG_MASK GENMASK(9, 0)
#define DME_VS_CORE_CLK_CTRL_DME_HW_CGC_EN             BIT(9)

/* Qualcomm MCQ Configuration */
#define UFS_QCOM_MCQCAP_QCFGPTR     224  /* 0xE0 in hex */
#define UFS_QCOM_MCQ_CONFIG_OFFSET  (UFS_QCOM_MCQCAP_QCFGPTR * 0x200)  /* 0x1C000 */

/* Doorbell offsets within MCQ region (relative to MCQ_CONFIG_BASE) */
#define UFS_QCOM_MCQ_SQD_OFFSET     0x5000
#define UFS_QCOM_MCQ_CQD_OFFSET     0x5080
#define UFS_QCOM_MCQ_SQIS_OFFSET    0x5040
#define UFS_QCOM_MCQ_CQIS_OFFSET    0x50C0
#define UFS_QCOM_MCQ_STRIDE         0x100

/* Calculated doorbell address offsets (relative to mmio_base) */
#define UFS_QCOM_SQD_ADDR_OFFSET    (UFS_QCOM_MCQ_CONFIG_OFFSET + UFS_QCOM_MCQ_SQD_OFFSET)
#define UFS_QCOM_CQD_ADDR_OFFSET    (UFS_QCOM_MCQ_CONFIG_OFFSET + UFS_QCOM_MCQ_CQD_OFFSET)
#define UFS_QCOM_SQIS_ADDR_OFFSET   (UFS_QCOM_MCQ_CONFIG_OFFSET + UFS_QCOM_MCQ_SQIS_OFFSET)
#define UFS_QCOM_CQIS_ADDR_OFFSET   (UFS_QCOM_MCQ_CONFIG_OFFSET + UFS_QCOM_MCQ_CQIS_OFFSET)
#define REG_UFS_MCQ_STRIDE          UFS_QCOM_MCQ_STRIDE

/* MCQ Vendor specific address offsets (relative to MCQ_CONFIG_BASE) */
#define UFS_MEM_VS_BASE 0x4000
#define UFS_MEM_CQIS_VS 0x4008

/* QCOM UFS host controller vendor specific registers */
enum {
	REG_UFS_SYS1CLK_1US                 = 0xC0,
	REG_UFS_TX_SYMBOL_CLK_NS_US         = 0xC4,
	REG_UFS_LOCAL_PORT_ID_REG           = 0xC8,
	REG_UFS_PA_ERR_CODE                 = 0xCC,
	/* On older UFS revisions, this register is called "RETRY_TIMER_REG" */
	REG_UFS_PARAM0                      = 0xD0,
	/* On older UFS revisions, this register is called "REG_UFS_PA_LINK_STARTUP_TIMER" */
	REG_UFS_CFG0                        = 0xD8,
	REG_UFS_CFG1                        = 0xDC,
	REG_UFS_CFG2                        = 0xE0,
	REG_UFS_HW_VERSION                  = 0xE4,

	UFS_TEST_BUS				= 0xE8,
	UFS_TEST_BUS_CTRL_0			= 0xEC,
	UFS_TEST_BUS_CTRL_1			= 0xF0,
	UFS_TEST_BUS_CTRL_2			= 0xF4,
	UFS_UNIPRO_CFG				= 0xF8,

	/*
	 * QCOM UFS host controller vendor specific registers
	 * added in HW Version 3.0.0
	 */
	UFS_AH8_CFG				= 0xFC,

	UFS_RD_REG_MCQ				= 0xD00,
	UFS_MEM_ICE_CFG				= 0x2600,
	REG_UFS_MEM_ICE_CONFIG			= 0x260C,
	REG_UFS_MEM_ICE_NUM_CORE		= 0x2664,

	REG_UFS_CFG3				= 0x271C,

	REG_UFS_DEBUG_SPARE_CFG			= 0x284C,
};

/* QCOM UFS host controller vendor specific debug registers */
enum {
	UFS_DBG_RD_REG_UAWM			= 0x100,
	UFS_DBG_RD_REG_UARM			= 0x200,
	UFS_DBG_RD_REG_TXUC			= 0x300,
	UFS_DBG_RD_REG_RXUC			= 0x400,
	UFS_DBG_RD_REG_DFC			= 0x500,
	UFS_DBG_RD_REG_TRLUT			= 0x600,
	UFS_DBG_RD_REG_TMRLUT			= 0x700,
	UFS_UFS_DBG_RD_REG_OCSC			= 0x800,

	UFS_UFS_DBG_RD_DESC_RAM			= 0x1500,
	UFS_UFS_DBG_RD_PRDT_RAM			= 0x1700,
	UFS_UFS_DBG_RD_RESP_RAM			= 0x1800,
	UFS_UFS_DBG_RD_EDTL_RAM			= 0x1900,
};

/* QCOM UFS HC vendor specific Hibern8 count registers */
enum {
	REG_UFS_HW_H8_ENTER_CNT			= 0x2700,
	REG_UFS_SW_H8_ENTER_CNT			= 0x2704,
	REG_UFS_SW_AFTER_HW_H8_ENTER_CNT	= 0x2708,
	REG_UFS_HW_H8_EXIT_CNT			= 0x270C,
	REG_UFS_SW_H8_EXIT_CNT			= 0x2710,
};

#define UFS_CNTLR_2_x_x_VEN_REGS_OFFSET(x)	(0x000 + x)
#define UFS_CNTLR_3_x_x_VEN_REGS_OFFSET(x)	(0x400 + x)

/* bit definitions for REG_UFS_CFG0 register */
#define QUNIPRO_G4_SEL		BIT(5)

/* bit definitions for REG_UFS_CFG1 register */
#define QUNIPRO_SEL		BIT(0)
#define UFS_PHY_SOFT_RESET	BIT(1)
#define UTP_DBG_RAMS_EN		BIT(17)
#define TEST_BUS_EN		BIT(18)
#define TEST_BUS_SEL		GENMASK(22, 19)
#define UFS_REG_TEST_BUS_EN	BIT(30)

/* bit definitions for REG_UFS_CFG2 register */
#define UAWM_HW_CGC_EN		BIT(0)
#define UARM_HW_CGC_EN		BIT(1)
#define TXUC_HW_CGC_EN		BIT(2)
#define RXUC_HW_CGC_EN		BIT(3)
#define DFC_HW_CGC_EN		BIT(4)
#define TRLUT_HW_CGC_EN		BIT(5)
#define TMRLUT_HW_CGC_EN	BIT(6)
#define OCSC_HW_CGC_EN		BIT(7)

/* bit definitions for REG_UFS_CFG3 register */
#define ESI_VEC_MASK		GENMASK(22, 12)

/* bit definitions for REG_UFS_PARAM0 */
#define MAX_HS_GEAR_MASK	GENMASK(6, 4)
#define UFS_QCOM_MAX_GEAR(x)	FIELD_GET(MAX_HS_GEAR_MASK, (x))

/* bit definition for UFS_UFS_TEST_BUS_CTRL_n */
#define TEST_BUS_SUB_SEL_MASK	GENMASK(4, 0)  /* All XXX_SEL fields are 5 bits wide */

/* bit definition for UFS Shared ICE config */
#define UFS_QCOM_CAP_ICE_CONFIG BIT(0)

#define REG_UFS_CFG2_CGC_EN_ALL (UAWM_HW_CGC_EN | UARM_HW_CGC_EN |\
				 TXUC_HW_CGC_EN | RXUC_HW_CGC_EN |\
				 DFC_HW_CGC_EN | TRLUT_HW_CGC_EN |\
				 TMRLUT_HW_CGC_EN | OCSC_HW_CGC_EN)

/* QUniPro Vendor specific attributes */
#define PA_TX_HSG1_SYNC_LENGTH	0x1552
#define PA_VS_CONFIG_REG1	0x9000
#define DME_VS_CORE_CLK_CTRL	0xD002
#define TX_HS_EQUALIZER		0x0037

/* bit and mask definitions for DME_VS_CORE_CLK_CTRL attribute */
#define CLK_1US_CYCLES_MASK_V4				GENMASK(27, 16)
#define CLK_1US_CYCLES_MASK				GENMASK(7, 0)
#define DME_VS_CORE_CLK_CTRL_CORE_CLK_DIV_EN_BIT	BIT(8)
#define PA_VS_CORE_CLK_40NS_CYCLES			0x9007
#define PA_VS_CORE_CLK_40NS_CYCLES_MASK			GENMASK(6, 0)


/* QCOM UFS host controller core clk frequencies */
#define UNIPRO_CORE_CLK_FREQ_37_5_MHZ          38
#define UNIPRO_CORE_CLK_FREQ_75_MHZ            75
#define UNIPRO_CORE_CLK_FREQ_100_MHZ           100
#define UNIPRO_CORE_CLK_FREQ_150_MHZ           150
#define UNIPRO_CORE_CLK_FREQ_300_MHZ           300
#define UNIPRO_CORE_CLK_FREQ_201_5_MHZ         202
#define UNIPRO_CORE_CLK_FREQ_403_MHZ           403

/* TX_HSG1_SYNC_LENGTH attr value */
#define PA_TX_HSG1_SYNC_LENGTH_VAL	0x4A

/*
 * Some ufs device vendors need a different TSync length.
 * Enable this quirk to give an additional TX_HS_SYNC_LENGTH.
 */
#define UFS_DEVICE_QUIRK_PA_TX_HSG1_SYNC_LENGTH		BIT(16)

/*
 * Some ufs device vendors need a different Deemphasis setting.
 * Enable this quirk to tune TX Deemphasis parameters.
 */
#define UFS_DEVICE_QUIRK_PA_TX_DEEMPHASIS_TUNING	BIT(17)

/* ICE allocator type to share AES engines among TX stream and RX stream */
#define ICE_ALLOCATOR_TYPE 2

/*
 * Number of cores allocated for RX stream when Read data block received and
 * Write data block is not in progress
 */
#define NUM_RX_R1W0 28

/*
 * Number of cores allocated for TX stream when Device asked to send write
 * data block and Read data block is not in progress
 */
#define NUM_TX_R0W1 28

/*
 * Number of cores allocated for RX stream when Read data block received and
 * Write data block is in progress
 * OR
 * Device asked to send write data block and Read data block is in progress
 */
#define NUM_RX_R1W1 15

/*
 * Number of cores allocated for TX stream (UFS write) when Read data block
 * received and Write data block is in progress
 * OR
 * Device asked to send write data block and Read data block is in progress
 */
#define NUM_TX_R1W1 13

static inline void
ufs_qcom_get_controller_revision(struct ufs_hba *hba,
				 u8 *major, u16 *minor, u16 *step)
{
	u32 ver = ufshcd_readl(hba, REG_UFS_HW_VERSION);

	*major = FIELD_GET(UFS_HW_VER_MAJOR_MASK, ver);
	*minor = FIELD_GET(UFS_HW_VER_MINOR_MASK, ver);
	*step = FIELD_GET(UFS_HW_VER_STEP_MASK, ver);
};

static inline void ufs_qcom_assert_reset(struct ufs_hba *hba)
{
	ufshcd_rmwl(hba, UFS_PHY_SOFT_RESET, UFS_PHY_SOFT_RESET, REG_UFS_CFG1);

	/*
	 * Dummy read to ensure the write takes effect before doing any sort
	 * of delay
	 */
	ufshcd_readl(hba, REG_UFS_CFG1);
}

static inline void ufs_qcom_deassert_reset(struct ufs_hba *hba)
{
	ufshcd_rmwl(hba, UFS_PHY_SOFT_RESET, 0, REG_UFS_CFG1);

	/*
	 * Dummy read to ensure the write takes effect before doing any sort
	 * of delay
	 */
	ufshcd_readl(hba, REG_UFS_CFG1);
}

/* Host controller hardware version: major.minor.step */
struct ufs_hw_version {
	u16 step;
	u16 minor;
	u8 major;
};

struct ufs_qcom_testbus {
	u8 select_major;
	u8 select_minor;
};

struct gpio_desc;

struct ufs_qcom_host {
	struct phy *generic_phy;
	struct ufs_hba *hba;
	struct ufs_pa_layer_attr dev_req_params;
	struct clk_bulk_data *clks;
	u32 num_clks;
	bool is_lane_clks_enabled;

	struct icc_path *icc_ddr;
	struct icc_path *icc_cpu;

#ifdef CONFIG_SCSI_UFS_CRYPTO
	struct qcom_ice *ice;
#endif
	u32 caps;
	void __iomem *dev_ref_clk_ctrl_mmio;
	bool is_dev_ref_clk_enabled;
	struct ufs_hw_version hw_ver;

	u32 dev_ref_clk_en_mask;

	struct ufs_qcom_testbus testbus;

	/* Reset control of HCI */
	struct reset_control *core_reset;
	struct reset_controller_dev rcdev;

	struct gpio_desc *device_reset;

	struct ufs_host_params host_params;
	u32 phy_gear;

	bool esi_enabled;
};

struct ufs_qcom_drvdata {
	enum ufshcd_quirks quirks;
	bool no_phy_retention;
};

static inline u32
ufs_qcom_get_debug_reg_offset(struct ufs_qcom_host *host, u32 reg)
{
	if (host->hw_ver.major <= 0x02)
		return UFS_CNTLR_2_x_x_VEN_REGS_OFFSET(reg);

	return UFS_CNTLR_3_x_x_VEN_REGS_OFFSET(reg);
};

#define ufs_qcom_is_link_off(hba) ufshcd_is_link_off(hba)
#define ufs_qcom_is_link_active(hba) ufshcd_is_link_active(hba)
#define ufs_qcom_is_link_hibern8(hba) ufshcd_is_link_hibern8(hba)
#define ceil(freq, div) ((freq) % (div) == 0 ? ((freq)/(div)) : ((freq)/(div) + 1))

int ufs_qcom_testbus_config(struct ufs_qcom_host *host);

#endif /* UFS_QCOM_H_ */
