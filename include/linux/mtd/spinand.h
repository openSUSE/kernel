/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2017 Micron Technology, Inc.
 *
 *  Authors:
 *	Peter Pan <peterpandong@micron.com>
 */
#ifndef __LINUX_MTD_SPINAND_H
#define __LINUX_MTD_SPINAND_H

#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/**
 * Standard SPI NAND flash operations
 */

#define SPINAND_RESET_1S_0_0_OP						\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xff, 1),				\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPINAND_WR_EN_DIS_1S_0_0_OP(enable)					\
	SPI_MEM_OP(SPI_MEM_OP_CMD((enable) ? 0x06 : 0x04, 1),		\
		   SPI_MEM_OP_NO_ADDR,					\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPINAND_READID_1S_1S_1S_OP(naddr, ndummy, buf, len)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x9f, 1),				\
		   SPI_MEM_OP_ADDR(naddr, 0, 1),			\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 1))

#define SPINAND_SET_FEATURE_1S_1S_1S_OP(reg, valptr)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x1f, 1),				\
		   SPI_MEM_OP_ADDR(1, reg, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(1, valptr, 1))

#define SPINAND_GET_FEATURE_1S_1S_1S_OP(reg, valptr)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x0f, 1),				\
		   SPI_MEM_OP_ADDR(1, reg, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_IN(1, valptr, 1))

#define SPINAND_BLK_ERASE_1S_1S_0_OP(addr)				\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xd8, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPINAND_PAGE_READ_1S_1S_0_OP(addr)				\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x13, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x03, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 1),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x0b, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 1),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_3A_1S_1S_1S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x03, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 1),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_FAST_3A_1S_1S_1S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x0b, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 1),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1D_1D_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x0d, 1),				\
		   SPI_MEM_DTR_OP_ADDR(2, addr, 1),			\
		   SPI_MEM_DTR_OP_DUMMY(ndummy, 1),			\
		   SPI_MEM_DTR_OP_DATA_IN(len, buf, 1),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x3b, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 2),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_3A_1S_1S_2S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x3b, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 2),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1D_2D_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x3d, 1),				\
		   SPI_MEM_DTR_OP_ADDR(2, addr, 1),			\
		   SPI_MEM_DTR_OP_DUMMY(ndummy, 1),			\
		   SPI_MEM_DTR_OP_DATA_IN(len, buf, 2),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_2S_2S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xbb, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 2),				\
		   SPI_MEM_OP_DUMMY(ndummy, 2),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 2),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_3A_1S_2S_2S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xbb, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 2),				\
		   SPI_MEM_OP_DUMMY(ndummy, 2),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 2),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_2D_2D_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xbd, 1),				\
		   SPI_MEM_DTR_OP_ADDR(2, addr, 2),			\
		   SPI_MEM_DTR_OP_DUMMY(ndummy, 2),			\
		   SPI_MEM_DTR_OP_DATA_IN(len, buf, 2),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x6b, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 4),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_3A_1S_1S_4S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x6b, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 4),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1D_4D_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x6d, 1),				\
		   SPI_MEM_DTR_OP_ADDR(2, addr, 1),			\
		   SPI_MEM_DTR_OP_DUMMY(ndummy, 1),			\
		   SPI_MEM_DTR_OP_DATA_IN(len, buf, 4),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_4S_4S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xeb, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 4),				\
		   SPI_MEM_OP_DUMMY(ndummy, 4),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 4),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_3A_1S_4S_4S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xeb, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 4),				\
		   SPI_MEM_OP_DUMMY(ndummy, 4),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 4),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_4D_4D_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xed, 1),				\
		   SPI_MEM_DTR_OP_ADDR(2, addr, 4),			\
		   SPI_MEM_DTR_OP_DUMMY(ndummy, 4),			\
		   SPI_MEM_DTR_OP_DATA_IN(len, buf, 4),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1S_8S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x8b, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_DUMMY(ndummy, 1),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 8),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_8S_8S_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0xcb, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 8),				\
		   SPI_MEM_OP_DUMMY(ndummy, 8),				\
		   SPI_MEM_OP_DATA_IN(len, buf, 8),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PAGE_READ_FROM_CACHE_1S_1D_8D_OP(addr, ndummy, buf, len, freq) \
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x9d, 1),				\
		   SPI_MEM_DTR_OP_ADDR(2, addr, 1),			\
		   SPI_MEM_DTR_OP_DUMMY(ndummy, 1),			\
		   SPI_MEM_DTR_OP_DATA_IN(len, buf, 8),			\
		   SPI_MEM_OP_MAX_FREQ(freq))

#define SPINAND_PROG_EXEC_1S_1S_0_OP(addr)				\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x10, 1),				\
		   SPI_MEM_OP_ADDR(3, addr, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_NO_DATA)

#define SPINAND_PROG_LOAD_1S_1S_1S_OP(reset, addr, buf, len)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(reset ? 0x02 : 0x84, 1),		\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(len, buf, 1))

#define SPINAND_PROG_LOAD_1S_1S_4S_OP(reset, addr, buf, len)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(reset ? 0x32 : 0x34, 1),		\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(len, buf, 4))

#define SPINAND_PROG_LOAD_1S_1S_8S_OP(addr, buf, len)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(0x82, 1),				\
		   SPI_MEM_OP_ADDR(2, addr, 1),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(len, buf, 8))

#define SPINAND_PROG_LOAD_1S_8S_8S_OP(reset, addr, buf, len)		\
	SPI_MEM_OP(SPI_MEM_OP_CMD(reset ? 0xc2 : 0xc4, 1),		\
		   SPI_MEM_OP_ADDR(2, addr, 8),				\
		   SPI_MEM_OP_NO_DUMMY,					\
		   SPI_MEM_OP_DATA_OUT(len, buf, 8))

/**
 * Standard SPI NAND flash commands
 */
#define SPINAND_CMD_PROG_LOAD_X4		0x32
#define SPINAND_CMD_PROG_LOAD_RDM_DATA_X4	0x34

/* feature register */
#define REG_BLOCK_LOCK		0xa0
#define BL_ALL_UNLOCKED		0x00

/* configuration register */
#define REG_CFG			0xb0
#define CFG_OTP_ENABLE		BIT(6)
#define CFG_ECC_ENABLE		BIT(4)
#define CFG_QUAD_ENABLE		BIT(0)

/* status register */
#define REG_STATUS		0xc0
#define STATUS_BUSY		BIT(0)
#define STATUS_ERASE_FAILED	BIT(2)
#define STATUS_PROG_FAILED	BIT(3)
#define STATUS_ECC_MASK		GENMASK(5, 4)
#define STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define STATUS_ECC_HAS_BITFLIPS	(1 << 4)
#define STATUS_ECC_UNCOR_ERROR	(2 << 4)

struct spinand_op;
struct spinand_device;

#define SPINAND_MAX_ID_LEN	5
/*
 * For erase, write and read operation, we got the following timings :
 * tBERS (erase) 1ms to 4ms
 * tPROG 300us to 400us
 * tREAD 25us to 100us
 * In order to minimize latency, the min value is divided by 4 for the
 * initial delay, and dividing by 20 for the poll delay.
 * For reset, 5us/10us/500us if the device is respectively
 * reading/programming/erasing when the RESET occurs. Since we always
 * issue a RESET when the device is IDLE, 5us is selected for both initial
 * and poll delay.
 */
#define SPINAND_READ_INITIAL_DELAY_US	6
#define SPINAND_READ_POLL_DELAY_US	5
#define SPINAND_RESET_INITIAL_DELAY_US	5
#define SPINAND_RESET_POLL_DELAY_US	5
#define SPINAND_WRITE_INITIAL_DELAY_US	75
#define SPINAND_WRITE_POLL_DELAY_US	15
#define SPINAND_ERASE_INITIAL_DELAY_US	250
#define SPINAND_ERASE_POLL_DELAY_US	50

#define SPINAND_WAITRDY_TIMEOUT_MS	400

/**
 * struct spinand_id - SPI NAND id structure
 * @data: buffer containing the id bytes. Currently 4 bytes large, but can
 *	  be extended if required
 * @len: ID length
 */
struct spinand_id {
	u8 data[SPINAND_MAX_ID_LEN];
	int len;
};

enum spinand_readid_method {
	SPINAND_READID_METHOD_OPCODE,
	SPINAND_READID_METHOD_OPCODE_ADDR,
	SPINAND_READID_METHOD_OPCODE_DUMMY,
};

/**
 * struct spinand_devid - SPI NAND device id structure
 * @id: device id of current chip
 * @len: number of bytes in device id
 * @method: method to read chip id
 *	    There are 3 possible variants:
 *	    SPINAND_READID_METHOD_OPCODE: chip id is returned immediately
 *	    after read_id opcode.
 *	    SPINAND_READID_METHOD_OPCODE_ADDR: chip id is returned after
 *	    read_id opcode + 1-byte address.
 *	    SPINAND_READID_METHOD_OPCODE_DUMMY: chip id is returned after
 *	    read_id opcode + 1 dummy byte.
 */
struct spinand_devid {
	const u8 *id;
	const u8 len;
	const enum spinand_readid_method method;
};

/**
 * struct manufacurer_ops - SPI NAND manufacturer specific operations
 * @init: initialize a SPI NAND device
 * @cleanup: cleanup a SPI NAND device
 *
 * Each SPI NAND manufacturer driver should implement this interface so that
 * NAND chips coming from this vendor can be initialized properly.
 */
struct spinand_manufacturer_ops {
	int (*init)(struct spinand_device *spinand);
	void (*cleanup)(struct spinand_device *spinand);
};

/**
 * struct spinand_manufacturer - SPI NAND manufacturer instance
 * @id: manufacturer ID
 * @name: manufacturer name
 * @devid_len: number of bytes in device ID
 * @chips: supported SPI NANDs under current manufacturer
 * @nchips: number of SPI NANDs available in chips array
 * @ops: manufacturer operations
 */
struct spinand_manufacturer {
	u8 id;
	char *name;
	const struct spinand_info *chips;
	const size_t nchips;
	const struct spinand_manufacturer_ops *ops;
};

/* SPI NAND manufacturers */
extern const struct spinand_manufacturer alliancememory_spinand_manufacturer;
extern const struct spinand_manufacturer ato_spinand_manufacturer;
extern const struct spinand_manufacturer esmt_c8_spinand_manufacturer;
extern const struct spinand_manufacturer fmsh_spinand_manufacturer;
extern const struct spinand_manufacturer foresee_spinand_manufacturer;
extern const struct spinand_manufacturer gigadevice_spinand_manufacturer;
extern const struct spinand_manufacturer macronix_spinand_manufacturer;
extern const struct spinand_manufacturer micron_spinand_manufacturer;
extern const struct spinand_manufacturer paragon_spinand_manufacturer;
extern const struct spinand_manufacturer skyhigh_spinand_manufacturer;
extern const struct spinand_manufacturer toshiba_spinand_manufacturer;
extern const struct spinand_manufacturer winbond_spinand_manufacturer;
extern const struct spinand_manufacturer xtx_spinand_manufacturer;

/**
 * struct spinand_op_variants - SPI NAND operation variants
 * @ops: the list of variants for a given operation
 * @nops: the number of variants
 *
 * Some operations like read-from-cache/write-to-cache have several variants
 * depending on the number of IO lines you use to transfer data or address
 * cycles. This structure is a way to describe the different variants supported
 * by a chip and let the core pick the best one based on the SPI mem controller
 * capabilities.
 */
struct spinand_op_variants {
	const struct spi_mem_op *ops;
	unsigned int nops;
};

#define SPINAND_OP_VARIANTS(name, ...)					\
	const struct spinand_op_variants name = {			\
		.ops = (struct spi_mem_op[]) { __VA_ARGS__ },		\
		.nops = sizeof((struct spi_mem_op[]){ __VA_ARGS__ }) /	\
			sizeof(struct spi_mem_op),			\
	}

/**
 * spinand_ecc_info - description of the on-die ECC implemented by a SPI NAND
 *		      chip
 * @get_status: get the ECC status. Should return a positive number encoding
 *		the number of corrected bitflips if correction was possible or
 *		-EBADMSG if there are uncorrectable errors. I can also return
 *		other negative error codes if the error is not caused by
 *		uncorrectable bitflips
 * @ooblayout: the OOB layout used by the on-die ECC implementation
 */
struct spinand_ecc_info {
	int (*get_status)(struct spinand_device *spinand, u8 status);
	const struct mtd_ooblayout_ops *ooblayout;
};

#define SPINAND_HAS_QE_BIT		BIT(0)
#define SPINAND_HAS_CR_FEAT_BIT		BIT(1)
#define SPINAND_HAS_PROG_PLANE_SELECT_BIT		BIT(2)
#define SPINAND_HAS_READ_PLANE_SELECT_BIT		BIT(3)
#define SPINAND_NO_RAW_ACCESS				BIT(4)

/**
 * struct spinand_ondie_ecc_conf - private SPI-NAND on-die ECC engine structure
 * @status: status of the last wait operation that will be used in case
 *          ->get_status() is not populated by the spinand device.
 */
struct spinand_ondie_ecc_conf {
	u8 status;
};

/**
 * struct spinand_otp_layout - structure to describe the SPI NAND OTP area
 * @npages: number of pages in the OTP
 * @start_page: start page of the user/factory OTP area.
 */
struct spinand_otp_layout {
	unsigned int npages;
	unsigned int start_page;
};

/**
 * struct spinand_fact_otp_ops - SPI NAND OTP methods for factory area
 * @info: get the OTP area information
 * @read: read from the SPI NAND OTP area
 */
struct spinand_fact_otp_ops {
	int (*info)(struct spinand_device *spinand, size_t len,
		    struct otp_info *buf, size_t *retlen);
	int (*read)(struct spinand_device *spinand, loff_t from, size_t len,
		    size_t *retlen, u8 *buf);
};

/**
 * struct spinand_user_otp_ops - SPI NAND OTP methods for user area
 * @info: get the OTP area information
 * @lock: lock an OTP region
 * @erase: erase an OTP region
 * @read: read from the SPI NAND OTP area
 * @write: write to the SPI NAND OTP area
 */
struct spinand_user_otp_ops {
	int (*info)(struct spinand_device *spinand, size_t len,
		    struct otp_info *buf, size_t *retlen);
	int (*lock)(struct spinand_device *spinand, loff_t from, size_t len);
	int (*erase)(struct spinand_device *spinand, loff_t from, size_t len);
	int (*read)(struct spinand_device *spinand, loff_t from, size_t len,
		    size_t *retlen, u8 *buf);
	int (*write)(struct spinand_device *spinand, loff_t from, size_t len,
		     size_t *retlen, const u8 *buf);
};

/**
 * struct spinand_fact_otp - SPI NAND OTP grouping structure for factory area
 * @layout: OTP region layout
 * @ops: OTP access ops
 */
struct spinand_fact_otp {
	const struct spinand_otp_layout layout;
	const struct spinand_fact_otp_ops *ops;
};

/**
 * struct spinand_user_otp - SPI NAND OTP grouping structure for user area
 * @layout: OTP region layout
 * @ops: OTP access ops
 */
struct spinand_user_otp {
	const struct spinand_otp_layout layout;
	const struct spinand_user_otp_ops *ops;
};

/**
 * struct spinand_info - Structure used to describe SPI NAND chips
 * @model: model name
 * @devid: device ID
 * @flags: OR-ing of the SPINAND_XXX flags
 * @memorg: memory organization
 * @eccreq: ECC requirements
 * @eccinfo: on-die ECC info
 * @op_variants: operations variants
 * @op_variants.read_cache: variants of the read-cache operation
 * @op_variants.write_cache: variants of the write-cache operation
 * @op_variants.update_cache: variants of the update-cache operation
 * @select_target: function used to select a target/die. Required only for
 *		   multi-die chips
 * @configure_chip: Align the chip configuration with the core settings
 * @set_cont_read: enable/disable continuous cached reads
 * @fact_otp: SPI NAND factory OTP info.
 * @user_otp: SPI NAND user OTP info.
 * @read_retries: the number of read retry modes supported
 * @set_read_retry: enable/disable read retry for data recovery
 *
 * Each SPI NAND manufacturer driver should have a spinand_info table
 * describing all the chips supported by the driver.
 */
struct spinand_info {
	const char *model;
	struct spinand_devid devid;
	u32 flags;
	struct nand_memory_organization memorg;
	struct nand_ecc_props eccreq;
	struct spinand_ecc_info eccinfo;
	struct {
		const struct spinand_op_variants *read_cache;
		const struct spinand_op_variants *write_cache;
		const struct spinand_op_variants *update_cache;
	} op_variants;
	int (*select_target)(struct spinand_device *spinand,
			     unsigned int target);
	int (*configure_chip)(struct spinand_device *spinand);
	int (*set_cont_read)(struct spinand_device *spinand,
			     bool enable);
	struct spinand_fact_otp fact_otp;
	struct spinand_user_otp user_otp;
	unsigned int read_retries;
	int (*set_read_retry)(struct spinand_device *spinand,
			     unsigned int read_retry);
};

#define SPINAND_ID(__method, ...)					\
	{								\
		.id = (const u8[]){ __VA_ARGS__ },			\
		.len = sizeof((u8[]){ __VA_ARGS__ }),			\
		.method = __method,					\
	}

#define SPINAND_INFO_OP_VARIANTS(__read, __write, __update)		\
	{								\
		.read_cache = __read,					\
		.write_cache = __write,					\
		.update_cache = __update,				\
	}

#define SPINAND_ECCINFO(__ooblayout, __get_status)			\
	.eccinfo = {							\
		.ooblayout = __ooblayout,				\
		.get_status = __get_status,				\
	}

#define SPINAND_SELECT_TARGET(__func)					\
	.select_target = __func

#define SPINAND_CONFIGURE_CHIP(__configure_chip)			\
	.configure_chip = __configure_chip

#define SPINAND_CONT_READ(__set_cont_read)				\
	.set_cont_read = __set_cont_read

#define SPINAND_FACT_OTP_INFO(__npages, __start_page, __ops)		\
	.fact_otp = {							\
		.layout = {						\
			.npages = __npages,				\
			.start_page = __start_page,			\
		},							\
		.ops = __ops,						\
	}

#define SPINAND_USER_OTP_INFO(__npages, __start_page, __ops)		\
	.user_otp = {							\
		.layout = {						\
			.npages = __npages,				\
			.start_page = __start_page,			\
		},							\
		.ops = __ops,						\
	}

#define SPINAND_READ_RETRY(__read_retries, __set_read_retry)		\
	.read_retries = __read_retries,					\
	.set_read_retry = __set_read_retry

#define SPINAND_INFO(__model, __id, __memorg, __eccreq, __op_variants,	\
		     __flags, ...)					\
	{								\
		.model = __model,					\
		.devid = __id,						\
		.memorg = __memorg,					\
		.eccreq = __eccreq,					\
		.op_variants = __op_variants,				\
		.flags = __flags,					\
		__VA_ARGS__						\
	}

struct spinand_dirmap {
	struct spi_mem_dirmap_desc *wdesc;
	struct spi_mem_dirmap_desc *rdesc;
	struct spi_mem_dirmap_desc *wdesc_ecc;
	struct spi_mem_dirmap_desc *rdesc_ecc;
};

/**
 * struct spinand_device - SPI NAND device instance
 * @base: NAND device instance
 * @spimem: pointer to the SPI mem object
 * @lock: lock used to serialize accesses to the NAND
 * @id: NAND ID as returned by READ_ID
 * @flags: NAND flags
 * @op_templates: various SPI mem op templates
 * @op_templates.read_cache: read cache op template
 * @op_templates.write_cache: write cache op template
 * @op_templates.update_cache: update cache op template
 * @select_target: select a specific target/die. Usually called before sending
 *		   a command addressing a page or an eraseblock embedded in
 *		   this die. Only required if your chip exposes several dies
 * @cur_target: currently selected target/die
 * @eccinfo: on-die ECC information
 * @cfg_cache: config register cache. One entry per die
 * @databuf: bounce buffer for data
 * @oobbuf: bounce buffer for OOB data
 * @scratchbuf: buffer used for everything but page accesses. This is needed
 *		because the spi-mem interface explicitly requests that buffers
 *		passed in spi_mem_op be DMA-able, so we can't based the bufs on
 *		the stack
 * @manufacturer: SPI NAND manufacturer information
 * @configure_chip: Align the chip configuration with the core settings
 * @cont_read_possible: Field filled by the core once the whole system
 *		configuration is known to tell whether continuous reads are
 *		suitable to use or not in general with this chip/configuration.
 *		A per-transfer check must of course be done to ensure it is
 *		actually relevant to enable this feature.
 * @set_cont_read: Enable/disable the continuous read feature
 * @priv: manufacturer private data
 * @fact_otp: SPI NAND factory OTP info.
 * @user_otp: SPI NAND user OTP info.
 * @read_retries: the number of read retry modes supported
 * @set_read_retry: Enable/disable the read retry feature
 */
struct spinand_device {
	struct nand_device base;
	struct spi_mem *spimem;
	struct mutex lock;
	struct spinand_id id;
	u32 flags;

	struct {
		const struct spi_mem_op *read_cache;
		const struct spi_mem_op *write_cache;
		const struct spi_mem_op *update_cache;
	} op_templates;

	struct spinand_dirmap *dirmaps;

	int (*select_target)(struct spinand_device *spinand,
			     unsigned int target);
	unsigned int cur_target;

	struct spinand_ecc_info eccinfo;

	u8 *cfg_cache;
	u8 *databuf;
	u8 *oobbuf;
	u8 *scratchbuf;
	const struct spinand_manufacturer *manufacturer;
	void *priv;

	int (*configure_chip)(struct spinand_device *spinand);
	bool cont_read_possible;
	int (*set_cont_read)(struct spinand_device *spinand,
			     bool enable);

	const struct spinand_fact_otp *fact_otp;
	const struct spinand_user_otp *user_otp;

	unsigned int read_retries;
	int (*set_read_retry)(struct spinand_device *spinand,
			     unsigned int retry_mode);
};

/**
 * mtd_to_spinand() - Get the SPI NAND device attached to an MTD instance
 * @mtd: MTD instance
 *
 * Return: the SPI NAND device attached to @mtd.
 */
static inline struct spinand_device *mtd_to_spinand(struct mtd_info *mtd)
{
	return container_of(mtd_to_nanddev(mtd), struct spinand_device, base);
}

/**
 * spinand_to_mtd() - Get the MTD device embedded in a SPI NAND device
 * @spinand: SPI NAND device
 *
 * Return: the MTD device embedded in @spinand.
 */
static inline struct mtd_info *spinand_to_mtd(struct spinand_device *spinand)
{
	return nanddev_to_mtd(&spinand->base);
}

/**
 * nand_to_spinand() - Get the SPI NAND device embedding an NAND object
 * @nand: NAND object
 *
 * Return: the SPI NAND device embedding @nand.
 */
static inline struct spinand_device *nand_to_spinand(struct nand_device *nand)
{
	return container_of(nand, struct spinand_device, base);
}

/**
 * spinand_to_nand() - Get the NAND device embedded in a SPI NAND object
 * @spinand: SPI NAND device
 *
 * Return: the NAND device embedded in @spinand.
 */
static inline struct nand_device *
spinand_to_nand(struct spinand_device *spinand)
{
	return &spinand->base;
}

/**
 * spinand_set_of_node - Attach a DT node to a SPI NAND device
 * @spinand: SPI NAND device
 * @np: DT node
 *
 * Attach a DT node to a SPI NAND device.
 */
static inline void spinand_set_of_node(struct spinand_device *spinand,
				       struct device_node *np)
{
	nanddev_set_of_node(&spinand->base, np);
}

int spinand_match_and_init(struct spinand_device *spinand,
			   const struct spinand_info *table,
			   unsigned int table_size,
			   enum spinand_readid_method rdid_method);

int spinand_upd_cfg(struct spinand_device *spinand, u8 mask, u8 val);
int spinand_read_reg_op(struct spinand_device *spinand, u8 reg, u8 *val);
int spinand_write_reg_op(struct spinand_device *spinand, u8 reg, u8 val);
int spinand_write_enable_op(struct spinand_device *spinand);
int spinand_select_target(struct spinand_device *spinand, unsigned int target);

int spinand_wait(struct spinand_device *spinand, unsigned long initial_delay_us,
		 unsigned long poll_delay_us, u8 *s);

int spinand_read_page(struct spinand_device *spinand,
		      const struct nand_page_io_req *req);

int spinand_write_page(struct spinand_device *spinand,
		       const struct nand_page_io_req *req);

size_t spinand_otp_page_size(struct spinand_device *spinand);
size_t spinand_fact_otp_size(struct spinand_device *spinand);
size_t spinand_user_otp_size(struct spinand_device *spinand);

int spinand_fact_otp_read(struct spinand_device *spinand, loff_t ofs,
			  size_t len, size_t *retlen, u8 *buf);
int spinand_user_otp_read(struct spinand_device *spinand, loff_t ofs,
			  size_t len, size_t *retlen, u8 *buf);
int spinand_user_otp_write(struct spinand_device *spinand, loff_t ofs,
			   size_t len, size_t *retlen, const u8 *buf);

int spinand_set_mtd_otp_ops(struct spinand_device *spinand);

#endif /* __LINUX_MTD_SPINAND_H */
