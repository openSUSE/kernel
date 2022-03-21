// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021, ASPEED Technology Inc.
// Authors: KuoHsiang Chou <kuohsiang_chou@aspeedtech.com>

#include <linux/firmware.h>
#include <linux/delay.h>
#include <drm/drm_print.h>
#include "ast_drv.h"

bool ast_dp_read_edid(struct drm_device *dev, u8 *ediddata)
{
	struct ast_private *ast = to_ast_private(dev);
	u8 i = 0, j = 0;

#ifdef DPControlPower
	u8 bDPState_Change = false;

	// Check DP power off or not.
	if (ast->ASTDP_State & AST_DP_PHY_SLEEP) {
		// DP power on
		ast_dp_PowerOnOff(dev, AST_DP_POWER_ON);
		bDPState_Change = true;
	}
#endif

	/*
	 * CRD1[b5]: DP MCU FW is executing
	 * CRDC[b0]: DP link success
	 * CRDF[b0]: DP HPD
	 * CRE5[b0]: Host reading EDID process is done
	 */
	if (!(ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, ASTDP_MCU_FW_EXECUTING) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDC, ASTDP_LINK_SUCCESS) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, ASTDP_HPD) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5,
								ASTDP_HOST_EDID_READ_DONE_MASK))) {
#ifdef DPControlPower
		// Set back power off
		if (bDPState_Change)
			ast_dp_PowerOnOff(dev, AST_DP_POWER_OFF);
#endif
		return false;
	}

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, (u8) ~ASTDP_HOST_EDID_READ_DONE_MASK,
							0x00);

	for (i = 0; i < 32; i++) {
		/*
		 * CRE4[7:0]: Read-Pointer for EDID (Unit: 4bytes); valid range: 0~64
		 */
		ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE4,
					(u8) ~ASTDP_EDID_READ_POINTER_MASK, (u8) i);
		j = 0;

		/*
		 * CRD7[b0]: valid flag for EDID
		 * CRD6[b0]: mirror read pointer for EDID
		 */
		while ((ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD7,
				ASTDP_EDID_VALID_FLAG_MASK) != 0x01) ||
			(ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD6,
						ASTDP_EDID_READ_POINTER_MASK) != i)) {
			mdelay(j+1);

			if (!(ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1,
							ASTDP_MCU_FW_EXECUTING) &&
				ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDC,
							ASTDP_LINK_SUCCESS) &&
				ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, ASTDP_HPD))) {
				ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5,
							(u8) ~ASTDP_HOST_EDID_READ_DONE_MASK,
							ASTDP_HOST_EDID_READ_DONE);
				return false;
			}

			j++;
			if (j > 200) {
				ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5,
							(u8) ~ASTDP_HOST_EDID_READ_DONE_MASK,
							ASTDP_HOST_EDID_READ_DONE);
				return false;
			}
		}

		*(ediddata) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT,
							0xD8, ASTDP_EDID_READ_DATA_MASK);
		*(ediddata + 1) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD9,
								ASTDP_EDID_READ_DATA_MASK);
		*(ediddata + 2) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDA,
								ASTDP_EDID_READ_DATA_MASK);
		*(ediddata + 3) = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDB,
								ASTDP_EDID_READ_DATA_MASK);

		if (i == 31) {
			/*
			 * For 128-bytes EDID_1.3,
			 * 1. Add the value of Bytes-126 to Bytes-127.
			 *		The Bytes-127 is Checksum. Sum of all 128bytes should
			 *		equal 0	(mod 256).
			 * 2. Modify Bytes-126 to be 0.
			 *		The Bytes-126 indicates the Number of extensions to
			 *		follow. 0 represents noextensions.
			 */
			*(ediddata + 3) = *(ediddata + 3) + *(ediddata + 2);
			*(ediddata + 2) = 0;
		}

		ediddata += 4;
	}

	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE5, (u8) ~ASTDP_HOST_EDID_READ_DONE_MASK,
							ASTDP_HOST_EDID_READ_DONE);

#ifdef DPControlPower
	// Set back power off
	if (bDPState_Change)
		ast_dp_PowerOnOff(dev, AST_DP_POWER_OFF);
#endif

	return true;
}

/*
 * Launch Aspeed DP
 */
bool ast_dp_launch(struct drm_device *dev, u8 bPower)
{
	u32 i = 0, j = 0, WaitCount = 1;
	u8 bDPTX = 0;
	u8 bDPExecute = 1;

	struct ast_private *ast = to_ast_private(dev);
	// S3 come back, need more time to wait BMC ready.
	if (bPower)
		WaitCount = 300;

	// Fill
	ast->tx_chip_type = AST_TX_NONE;

	// Wait total count by different condition.
	// This is a temp solution for DP check
	for (j = 0; j < WaitCount; j++) {
		bDPTX = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, TX_TYPE_MASK);

		if (bDPTX)
			break;

		msleep(100);
	}

	// 0xE : ASTDP with DPMCU FW handling
	if (bDPTX == ASTDP_DPMCU_TX) {
		// Wait one second then timeout.
		i = 0;

		while (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xD1, COPROCESSOR_LAUNCH) !=
			COPROCESSOR_LAUNCH) {
			i++;
			// wait 100 ms
			msleep(100);

			if (i >= 10) {
				// DP would not be ready.
				bDPExecute = 0;
				break;
			}
		}

		if (bDPExecute)
			ast->tx_chip_type = AST_TX_ASTDP;
	}

	return true;
}

#ifdef DPControlPower

void ast_dp_PowerOnOff(struct drm_device *dev, u8 Mode)
{
	struct ast_private *ast = to_ast_private(dev);
	// Read and Turn off DP PHY sleep
	u8 bE3 = ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE3, AST_DP_VIDEO_ENABLE);

	// Turn on DP PHY sleep
	if (!Mode)
		bE3 |= AST_DP_PHY_SLEEP;

	// DP Power on/off
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE3, (u8) ~AST_DP_PHY_SLEEP, bE3);

	// Save ASTDP power state
	ast->ASTDP_State = bE3;
}

#endif

void ast_dp_SetOnOff(struct drm_device *dev, u8 Mode)
{
	struct ast_private *ast = to_ast_private(dev);

	// Video On/Off
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE3, (u8) ~AST_DP_VIDEO_ENABLE, Mode);

	// Save ASTDP power state
	ast->ASTDP_State = Mode;

	// If DP plug in and link successful then check video on / off status
	if (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDC, ASTDP_LINK_SUCCESS) &&
		ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF, ASTDP_HPD)) {
		Mode <<= 4;
		while (ast_get_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xDF,
						ASTDP_MIRROR_VIDEO_ENABLE) != Mode) {
			// wait 1 ms
			mdelay(1);
		}
	}
}

void ast_dp_SetOutput(struct drm_crtc *crtc, struct ast_vbios_mode_info *vbios_mode)
{
	struct ast_private *ast = to_ast_private(crtc->dev);

	u32 ulRefreshRateIndex;
	u8 ModeIdx;

	ulRefreshRateIndex = vbios_mode->enh_table->refresh_rate_index - 1;

	switch (crtc->mode.crtc_hdisplay) {
	case 320:
		ModeIdx = ASTDP_320x240_60;
		break;
	case 400:
		ModeIdx = ASTDP_400x300_60;
		break;
	case 512:
		ModeIdx = ASTDP_512x384_60;
		break;
	case 640:
		ModeIdx = (ASTDP_640x480_60 + (u8) ulRefreshRateIndex);
		break;
	case 800:
		ModeIdx = (ASTDP_800x600_56 + (u8) ulRefreshRateIndex);
		break;
	case 1024:
		ModeIdx = (ASTDP_1024x768_60 + (u8) ulRefreshRateIndex);
		break;
	case 1152:
		ModeIdx = ASTDP_1152x864_75;
		break;
	case 1280:
		if (crtc->mode.crtc_vdisplay == 800)
			ModeIdx = (ASTDP_1280x800_60_RB - (u8) ulRefreshRateIndex);
		else		// 1024
			ModeIdx = (ASTDP_1280x1024_60 + (u8) ulRefreshRateIndex);
		break;
	case 1360:
	case 1366:
		ModeIdx = ASTDP_1366x768_60;
		break;
	case 1440:
		ModeIdx = (ASTDP_1440x900_60_RB - (u8) ulRefreshRateIndex);
		break;
	case 1600:
		if (crtc->mode.crtc_vdisplay == 900)
			ModeIdx = (ASTDP_1600x900_60_RB - (u8) ulRefreshRateIndex);
		else		//1200
			ModeIdx = ASTDP_1600x1200_60;
		break;
	case 1680:
		ModeIdx = (ASTDP_1680x1050_60_RB - (u8) ulRefreshRateIndex);
		break;
	case 1920:
		if (crtc->mode.crtc_vdisplay == 1080)
			ModeIdx = ASTDP_1920x1080_60;
		else		//1200
			ModeIdx = ASTDP_1920x1200_60;
		break;
	default:
		return;
	}

	/*
	 * CRE0[7:0]: MISC0 ((0x00: 18-bpp) or (0x20: 24-bpp)
	 * CRE1[7:0]: MISC1 (default: 0x00)
	 * CRE2[7:0]: video format index (0x00 ~ 0x20 or 0x40 ~ 0x50)
	 */
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE0, (u8) ~ASTDP_CLEAR_MASK,
				ASTDP_MISC0_24bpp);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE1, (u8) ~ASTDP_CLEAR_MASK, ASTDP_MISC1);
	ast_set_index_reg_mask(ast, AST_IO_CRTC_PORT, 0xE2, (u8) ~ASTDP_CLEAR_MASK, ModeIdx);
}
