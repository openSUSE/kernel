// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024-2026 Analog Devices, Inc.
 * Author: Radu Sabau <radu.sabau@analog.com>
 */
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/unaligned.h>

#include <linux/iio/iio.h>

#define AD4691_VREF_uV_MIN			2400000
#define AD4691_VREF_uV_MAX			5250000
#define AD4691_VREF_2P5_uV_MAX			2750000
#define AD4691_VREF_3P0_uV_MAX			3250000
#define AD4691_VREF_3P3_uV_MAX			3750000
#define AD4691_VREF_4P096_uV_MAX		4500000

#define AD4691_CNV_DUTY_CYCLE_NS		380
#define AD4691_CNV_HIGH_TIME_NS			430

#define AD4691_SPI_CONFIG_A_REG			0x000
#define AD4691_SW_RESET				(BIT(7) | BIT(0))

#define AD4691_STATUS_REG			0x014
#define AD4691_CLAMP_STATUS1_REG		0x01A
#define AD4691_CLAMP_STATUS2_REG		0x01B
#define AD4691_DEVICE_SETUP			0x020
#define AD4691_MANUAL_MODE			BIT(2)
#define AD4691_LDO_EN				BIT(4)
#define AD4691_REF_CTRL				0x021
#define AD4691_REF_CTRL_MASK			GENMASK(4, 2)
#define AD4691_REFBUF_EN			BIT(0)
#define AD4691_OSC_FREQ_REG			0x023
#define AD4691_OSC_FREQ_MASK			GENMASK(3, 0)
#define AD4691_STD_SEQ_CONFIG			0x025
#define AD4691_SEQ_ALL_CHANNELS_OFF		0x00
#define AD4691_SPARE_CONTROL			0x02A

#define AD4691_MAX_CHANNELS			16

#define AD4691_NOOP				0x00
#define AD4691_ADC_CHAN(ch)			((0x10 + (ch)) << 3)

#define AD4691_OSC_EN_REG			0x180
#define AD4691_STATE_RESET_REG			0x181
#define AD4691_STATE_RESET_ALL			BIT(0)
#define AD4691_ADC_SETUP			0x182
#define AD4691_ADC_MODE_MASK			GENMASK(1, 0)
#define AD4691_CNV_BURST_MODE			0x01
#define AD4691_AUTONOMOUS_MODE			0x02
/*
 * ACC_MASK_REG covers both mask bytes via ADDR_DESCENDING SPI: writing a
 * 16-bit BE value to 0x185 auto-decrements to 0x184 for the second byte.
 */
#define AD4691_ACC_MASK_REG			0x185
#define AD4691_ACC_DEPTH_IN(n)			(0x186 + (n))
#define AD4691_GPIO_MODE1_REG			0x196
#define AD4691_GPIO_MODE2_REG			0x197
#define AD4691_GP_MODE_MASK			GENMASK(3, 0)
#define AD4691_GP_MODE_DATA_READY		0x06
#define AD4691_GPIO_READ			0x1A0
#define AD4691_ACC_STATUS_FULL1_REG		0x1B0
#define AD4691_ACC_STATUS_FULL2_REG		0x1B1
#define AD4691_ACC_STATUS_OVERRUN1_REG		0x1B2
#define AD4691_ACC_STATUS_OVERRUN2_REG		0x1B3
#define AD4691_ACC_STATUS_SAT1_REG		0x1B4
#define AD4691_ACC_STATUS_SAT2_REG		0x1BE
#define AD4691_ACC_SAT_OVR_REG(n)		(0x1C0 + (n))
#define AD4691_AVG_IN(n)			(0x201 + (2 * (n)))
#define AD4691_AVG_STS_IN(n)			(0x222 + (3 * (n)))
#define AD4691_ACC_IN(n)			(0x252 + (3 * (n)))
#define AD4691_ACC_STS_DATA(n)			(0x283 + (4 * (n)))


static const char * const ad4691_supplies[] = { "avdd", "vio" };

enum ad4691_ref_ctrl {
	AD4691_VREF_2P5,
	AD4691_VREF_3P0,
	AD4691_VREF_3P3,
	AD4691_VREF_4P096,
	AD4691_VREF_5P0
};

struct ad4691_channel_info {
	const struct iio_chan_spec *channels __counted_by_ptr(num_channels);
	unsigned int num_channels;
};

struct ad4691_chip_info {
	const char *name;
	unsigned int max_rate;
	const struct ad4691_channel_info *sw_info;
};

#define AD4691_CHANNEL(ch)						\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE)	\
				    | BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
		.info_mask_shared_by_all_available =			\
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
		.channel = ch,						\
		.scan_type = {						\
			.realbits = 16,					\
		},							\
	}

static const struct iio_chan_spec ad4691_channels[] = {
	AD4691_CHANNEL(0),
	AD4691_CHANNEL(1),
	AD4691_CHANNEL(2),
	AD4691_CHANNEL(3),
	AD4691_CHANNEL(4),
	AD4691_CHANNEL(5),
	AD4691_CHANNEL(6),
	AD4691_CHANNEL(7),
	AD4691_CHANNEL(8),
	AD4691_CHANNEL(9),
	AD4691_CHANNEL(10),
	AD4691_CHANNEL(11),
	AD4691_CHANNEL(12),
	AD4691_CHANNEL(13),
	AD4691_CHANNEL(14),
	AD4691_CHANNEL(15),
};

static const struct iio_chan_spec ad4693_channels[] = {
	AD4691_CHANNEL(0),
	AD4691_CHANNEL(1),
	AD4691_CHANNEL(2),
	AD4691_CHANNEL(3),
	AD4691_CHANNEL(4),
	AD4691_CHANNEL(5),
	AD4691_CHANNEL(6),
	AD4691_CHANNEL(7),
};

static const struct ad4691_channel_info ad4691_sw_info = {
	.channels = ad4691_channels,
	.num_channels = ARRAY_SIZE(ad4691_channels),
};

static const struct ad4691_channel_info ad4693_sw_info = {
	.channels = ad4693_channels,
	.num_channels = ARRAY_SIZE(ad4693_channels),
};

/*
 * Internal oscillator frequency table. Index is the OSC_FREQ_REG[3:0] value.
 * Index 0 (1 MHz) is only valid for AD4692/AD4694; AD4691/AD4693 support
 * up to 500 kHz and use index 1 as their highest valid rate.
 */
static const int ad4691_osc_freqs_Hz[] = {
	[0x0] = 1000000,
	[0x1] = 500000,
	[0x2] = 400000,
	[0x3] = 250000,
	[0x4] = 200000,
	[0x5] = 167000,
	[0x6] = 133000,
	[0x7] = 125000,
	[0x8] = 100000,
	[0x9] = 50000,
	[0xA] = 25000,
	[0xB] = 12500,
	[0xC] = 10000,
	[0xD] = 5000,
	[0xE] = 2500,
	[0xF] = 1250,
};

static const struct ad4691_chip_info ad4691_chip_info = {
	.name = "ad4691",
	.max_rate = 500 * HZ_PER_KHZ,
	.sw_info = &ad4691_sw_info,
};

static const struct ad4691_chip_info ad4692_chip_info = {
	.name = "ad4692",
	.max_rate = 1 * HZ_PER_MHZ,
	.sw_info = &ad4691_sw_info,
};

static const struct ad4691_chip_info ad4693_chip_info = {
	.name = "ad4693",
	.max_rate = 500 * HZ_PER_KHZ,
	.sw_info = &ad4693_sw_info,
};

static const struct ad4691_chip_info ad4694_chip_info = {
	.name = "ad4694",
	.max_rate = 1 * HZ_PER_MHZ,
	.sw_info = &ad4693_sw_info,
};

struct ad4691_state {
	const struct ad4691_chip_info *info;
	struct regmap *regmap;
	struct spi_device *spi;

	int vref_uV;

	bool refbuf_en;
	bool ldo_en;
	/*
	 * Synchronize access to members of the driver state, and ensure
	 * atomicity of consecutive SPI operations.
	 */
	struct mutex lock;
};

static int ad4691_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct spi_device *spi = context;
	u8 tx[2], rx[4];
	int ret;

	/* Set bit 15 to mark the operation as READ. */
	put_unaligned_be16(0x8000 | reg, tx);

	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_SPARE_CONTROL ... AD4691_ACC_MASK_REG - 1:
	case AD4691_ACC_MASK_REG + 1 ... AD4691_ACC_SAT_OVR_REG(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 1);
		if (ret)
			return ret;
		*val = rx[0];
		return 0;
	case AD4691_ACC_MASK_REG:
	case AD4691_STD_SEQ_CONFIG:
	case AD4691_AVG_IN(0) ... AD4691_AVG_IN(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 2);
		if (ret)
			return ret;
		*val = get_unaligned_be16(rx);
		return 0;
	case AD4691_AVG_STS_IN(0) ... AD4691_AVG_STS_IN(15):
	case AD4691_ACC_IN(0) ... AD4691_ACC_IN(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 3);
		if (ret)
			return ret;
		*val = get_unaligned_be24(rx);
		return 0;
	case AD4691_ACC_STS_DATA(0) ... AD4691_ACC_STS_DATA(15):
		ret = spi_write_then_read(spi, tx, sizeof(tx), rx, 4);
		if (ret)
			return ret;
		*val = get_unaligned_be32(rx);
		return 0;
	default:
		return -EINVAL;
	}
}

static int ad4691_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct spi_device *spi = context;
	u8 tx[4];

	put_unaligned_be16(reg, tx);

	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_SPARE_CONTROL ... AD4691_ACC_MASK_REG - 1:
	case AD4691_ACC_MASK_REG + 1 ... AD4691_GPIO_MODE2_REG:
		if (val > U8_MAX)
			return -EINVAL;
		tx[2] = val;
		return spi_write_then_read(spi, tx, 3, NULL, 0);
	case AD4691_ACC_MASK_REG:
	case AD4691_STD_SEQ_CONFIG:
		if (val > U16_MAX)
			return -EINVAL;
		put_unaligned_be16(val, &tx[2]);
		return spi_write_then_read(spi, tx, 4, NULL, 0);
	default:
		return -EINVAL;
	}
}

static bool ad4691_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AD4691_STATUS_REG:
	case AD4691_CLAMP_STATUS1_REG:
	case AD4691_CLAMP_STATUS2_REG:
	case AD4691_GPIO_READ:
	case AD4691_ACC_STATUS_FULL1_REG ... AD4691_ACC_STATUS_SAT2_REG:
	case AD4691_ACC_SAT_OVR_REG(0) ... AD4691_ACC_SAT_OVR_REG(15):
	case AD4691_AVG_IN(0) ... AD4691_AVG_IN(15):
	case AD4691_AVG_STS_IN(0) ... AD4691_AVG_STS_IN(15):
	case AD4691_ACC_IN(0) ... AD4691_ACC_IN(15):
	case AD4691_ACC_STS_DATA(0) ... AD4691_ACC_STS_DATA(15):
		return true;
	default:
		return false;
	}
}

static bool ad4691_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_SPARE_CONTROL ... AD4691_ACC_SAT_OVR_REG(15):
	case AD4691_STD_SEQ_CONFIG:
		return true;
	default:
		break;
	}

	/*
	 * Multi-byte result registers have non-unit strides; only the base
	 * address of each entry is a valid single-register read.
	 */
	if (reg >= AD4691_AVG_IN(0) && reg <= AD4691_AVG_IN(15))
		return (reg - AD4691_AVG_IN(0)) % 2 == 0;
	if (reg >= AD4691_AVG_STS_IN(0) && reg <= AD4691_AVG_STS_IN(15))
		return (reg - AD4691_AVG_STS_IN(0)) % 3 == 0;
	if (reg >= AD4691_ACC_IN(0) && reg <= AD4691_ACC_IN(15))
		return (reg - AD4691_ACC_IN(0)) % 3 == 0;
	if (reg >= AD4691_ACC_STS_DATA(0) && reg <= AD4691_ACC_STS_DATA(15))
		return (reg - AD4691_ACC_STS_DATA(0)) % 4 == 0;

	return false;
}

static bool ad4691_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0 ... AD4691_OSC_FREQ_REG:
	case AD4691_STD_SEQ_CONFIG:
	case AD4691_SPARE_CONTROL ... AD4691_GPIO_MODE2_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ad4691_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_read = ad4691_reg_read,
	.reg_write = ad4691_reg_write,
	.volatile_reg = ad4691_volatile_reg,
	.readable_reg = ad4691_readable_reg,
	.writeable_reg = ad4691_writeable_reg,
	.max_register = AD4691_ACC_STS_DATA(15),
	.cache_type = REGCACHE_MAPLE,
};

/*
 * Index 0 in ad4691_osc_freqs_Hz is 1 MHz — valid only for AD4692/AD4694
 * (max_rate == 1 MHz). AD4691/AD4693 cap at 500 kHz so their valid range
 * starts at index 1.
 */
static unsigned int ad4691_samp_freq_start(const struct ad4691_chip_info *info)
{
	return (info->max_rate == 1 * HZ_PER_MHZ) ? 0 : 1;
}

static int ad4691_get_sampling_freq(struct ad4691_state *st, int *val)
{
	unsigned int reg_val;
	int ret;

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, AD4691_OSC_FREQ_REG, &reg_val);
	if (ret)
		return ret;

	*val = ad4691_osc_freqs_Hz[FIELD_GET(AD4691_OSC_FREQ_MASK, reg_val)];
	return IIO_VAL_INT;
}

static int ad4691_set_sampling_freq(struct ad4691_state *st, int freq)
{
	unsigned int start = ad4691_samp_freq_start(st->info);

	guard(mutex)(&st->lock);

	for (unsigned int i = start; i < ARRAY_SIZE(ad4691_osc_freqs_Hz); i++) {
		if (ad4691_osc_freqs_Hz[i] != freq)
			continue;
		return regmap_update_bits(st->regmap, AD4691_OSC_FREQ_REG,
					  AD4691_OSC_FREQ_MASK, i);
	}

	return -EINVAL;
}

static int ad4691_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type,
			     int *length, long mask)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	unsigned int start = ad4691_samp_freq_start(st->info);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = &ad4691_osc_freqs_Hz[start];
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(ad4691_osc_freqs_Hz) - start;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ad4691_single_shot_read(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan, int *val)
{
	struct ad4691_state *st = iio_priv(indio_dev);
	unsigned int reg_val, osc_idx, period_us;
	int ret;

	guard(mutex)(&st->lock);

	/* Use AUTONOMOUS mode for single-shot reads. */
	ret = regmap_write(st->regmap, AD4691_STATE_RESET_REG, AD4691_STATE_RESET_ALL);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_STD_SEQ_CONFIG,
			   BIT(chan->channel));
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_ACC_MASK_REG,
			   ~BIT(chan->channel) & GENMASK(15, 0));
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4691_OSC_FREQ_REG, &reg_val);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD4691_OSC_EN_REG, 1);
	if (ret)
		return ret;

	osc_idx = FIELD_GET(AD4691_OSC_FREQ_MASK, reg_val);
	/* Wait 2 oscillator periods for the conversion to complete. */
	period_us = DIV_ROUND_UP(2UL * USEC_PER_SEC, ad4691_osc_freqs_Hz[osc_idx]);
	fsleep(period_us);

	ret = regmap_write(st->regmap, AD4691_OSC_EN_REG, 0);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4691_AVG_IN(chan->channel), &reg_val);
	if (ret)
		return ret;

	*val = reg_val;

	ret = regmap_write(st->regmap, AD4691_STATE_RESET_REG, AD4691_STATE_RESET_ALL);
	if (ret)
		return ret;

	return IIO_VAL_INT;
}

static int ad4691_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long info)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW: {
		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		return ad4691_single_shot_read(indio_dev, chan, val);
	}
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad4691_get_sampling_freq(st, val);
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_uV / (MICRO / MILLI);
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int ad4691_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad4691_set_sampling_freq(st, val);
	default:
		return -EINVAL;
	}
}

static int ad4691_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct ad4691_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static const struct iio_info ad4691_info = {
	.read_raw = ad4691_read_raw,
	.write_raw = ad4691_write_raw,
	.read_avail = ad4691_read_avail,
	.debugfs_reg_access = ad4691_reg_access,
};

static int ad4691_regulator_setup(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	int ret;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ad4691_supplies),
					     ad4691_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get and enable supplies\n");

	/*
	 * vdd-supply and ldo-in-supply are mutually exclusive:
	 *   vdd-supply present  → external 1.8V VDD; disable internal LDO.
	 *   vdd-supply absent   → enable internal LDO fed from ldo-in-supply.
	 * Having both simultaneously is strongly inadvisable per the datasheet.
	 */
	if (device_property_present(dev, "vdd-supply")) {
		ret = devm_regulator_get_enable(dev, "vdd");
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get and enable VDD\n");
	} else if (device_property_present(dev, "ldo-in-supply")) {
		ret = devm_regulator_get_enable(dev, "ldo-in");
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get and enable LDO-IN\n");
		st->ldo_en = true;
	} else {
		return dev_err_probe(dev, -EINVAL,
				     "missing one of vdd-supply, ldo-in-supply\n");
	}

	if (device_property_present(dev, "ref-supply")) {
		st->vref_uV = devm_regulator_get_enable_read_voltage(dev, "ref");
		if (st->vref_uV < 0)
			return dev_err_probe(dev, st->vref_uV,
					     "Failed to get REF supply voltage\n");
	} else if (device_property_present(dev, "refin-supply")) {
		st->vref_uV = devm_regulator_get_enable_read_voltage(dev, "refin");
		if (st->vref_uV < 0)
			return dev_err_probe(dev, st->vref_uV,
					     "Failed to get REFIN supply voltage\n");
		st->refbuf_en = true;
	} else {
		return dev_err_probe(dev, -EINVAL,
				     "missing one of ref-supply, refin-supply\n");
	}

	if (st->vref_uV < AD4691_VREF_uV_MIN || st->vref_uV > AD4691_VREF_uV_MAX)
		return dev_err_probe(dev, -EINVAL,
				     "vref(%d) must be in the range [%u...%u]\n",
				     st->vref_uV, AD4691_VREF_uV_MIN,
				     AD4691_VREF_uV_MAX);

	return 0;
}

static int ad4691_reset(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct reset_control *rst;
	int ret;

	rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "Failed to get reset\n");

	if (rst) {
		/*
		 * Assert the reset line to guarantee a clean reset pulse on
		 * every probe, including driver reloads where the line may
		 * already be deasserted (reset_control_put() does not
		 * re-assert on release). tRESETL (minimum pulse width) = 10 ns
		 * (Table 5); kernel function-call overhead alone exceeds this,
		 * so no explicit delay is needed between assert and deassert.
		 */
		reset_control_assert(rst);
		ret = reset_control_deassert(rst);
		if (ret)
			return ret;
	} else {
		/* No hardware reset available, fall back to software reset. */
		ret = regmap_write(st->regmap, AD4691_SPI_CONFIG_A_REG,
				   AD4691_SW_RESET);
		if (ret)
			return ret;
	}

	/*
	 * Wait 300 µs (Table 5) for the device to complete its internal reset
	 * sequence before accepting SPI commands.
	 */
	fsleep(300);
	return 0;
}

static int ad4691_config(struct ad4691_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	enum ad4691_ref_ctrl ref_val;
	unsigned int val;
	int ret;

	switch (st->vref_uV) {
	case AD4691_VREF_uV_MIN ... AD4691_VREF_2P5_uV_MAX:
		ref_val = AD4691_VREF_2P5;
		break;
	case AD4691_VREF_2P5_uV_MAX + 1 ... AD4691_VREF_3P0_uV_MAX:
		ref_val = AD4691_VREF_3P0;
		break;
	case AD4691_VREF_3P0_uV_MAX + 1 ... AD4691_VREF_3P3_uV_MAX:
		ref_val = AD4691_VREF_3P3;
		break;
	case AD4691_VREF_3P3_uV_MAX + 1 ... AD4691_VREF_4P096_uV_MAX:
		ref_val = AD4691_VREF_4P096;
		break;
	case AD4691_VREF_4P096_uV_MAX + 1 ... AD4691_VREF_uV_MAX:
		ref_val = AD4691_VREF_5P0;
		break;
	default:
		return dev_err_probe(dev, -EINVAL,
				     "Unsupported vref voltage: %d uV\n",
				     st->vref_uV);
	}

	val = FIELD_PREP(AD4691_REF_CTRL_MASK, ref_val);
	if (st->refbuf_en)
		val |= AD4691_REFBUF_EN;

	ret = regmap_write(st->regmap, AD4691_REF_CTRL, val);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write REF_CTRL\n");

	ret = regmap_assign_bits(st->regmap, AD4691_DEVICE_SETUP,
				 AD4691_LDO_EN, st->ldo_en);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write DEVICE_SETUP\n");

	/*
	 * Set the internal oscillator to the highest rate this chip supports.
	 * Index 0 (1 MHz) exceeds the 500 kHz max of AD4691/AD4693, so those
	 * chips start at index 1 (500 kHz).
	 */
	ret = regmap_write(st->regmap, AD4691_OSC_FREQ_REG,
			   ad4691_samp_freq_start(st->info));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write OSC_FREQ\n");

	ret = regmap_update_bits(st->regmap, AD4691_ADC_SETUP,
				 AD4691_ADC_MODE_MASK, AD4691_AUTONOMOUS_MODE);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to write ADC_SETUP\n");

	return 0;
}

static int ad4691_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad4691_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -ENODEV;

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->regmap = devm_regmap_init(dev, NULL, spi, &ad4691_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to initialize regmap\n");

	ret = ad4691_regulator_setup(st);
	if (ret)
		return ret;

	ret = ad4691_reset(st);
	if (ret)
		return ret;

	ret = ad4691_config(st);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->info = &ad4691_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = st->info->sw_info->channels;
	indio_dev->num_channels = st->info->sw_info->num_channels;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad4691_of_match[] = {
	{ .compatible = "adi,ad4691", .data = &ad4691_chip_info },
	{ .compatible = "adi,ad4692", .data = &ad4692_chip_info },
	{ .compatible = "adi,ad4693", .data = &ad4693_chip_info },
	{ .compatible = "adi,ad4694", .data = &ad4694_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad4691_of_match);

static const struct spi_device_id ad4691_id[] = {
	{ .name = "ad4691", .driver_data = (kernel_ulong_t)&ad4691_chip_info },
	{ .name = "ad4692", .driver_data = (kernel_ulong_t)&ad4692_chip_info },
	{ .name = "ad4693", .driver_data = (kernel_ulong_t)&ad4693_chip_info },
	{ .name = "ad4694", .driver_data = (kernel_ulong_t)&ad4694_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad4691_id);

static struct spi_driver ad4691_driver = {
	.driver = {
		.name = "ad4691",
		.of_match_table = ad4691_of_match,
	},
	.probe = ad4691_probe,
	.id_table = ad4691_id,
};
module_spi_driver(ad4691_driver);

MODULE_AUTHOR("Radu Sabau <radu.sabau@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD4691 Family ADC Driver");
MODULE_LICENSE("GPL");
