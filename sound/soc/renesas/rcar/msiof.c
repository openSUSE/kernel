// SPDX-License-Identifier: GPL-2.0
//
// Renesas R-Car MSIOF (Clock-Synchronized Serial Interface with FIFO) I2S driver
//
// Copyright (C) 2025 Renesas Solutions Corp.
// Author: Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//

/*
 * [NOTE-CLOCK-MODE]
 *
 * This driver doesn't support Clock/Frame Provider Mode
 *
 * Basically MSIOF is created for SPI, but we can use it as I2S (Sound), etc. Because of it, when
 * we use it as I2S (Sound) with Provider Mode, we need to send dummy TX data even though it was
 * used for RX. Because SPI HW needs TX Clock/Frame output for RX purpose.
 * But it makes driver code complex in I2S (Sound).
 *
 * And when we use it as I2S (Sound) as Provider Mode, the clock source is [MSO clock] (= 133.33MHz)
 * SoC internal clock. It is not for 48kHz/44.1kHz base clock. Thus the output/input will not be
 * accurate sound.
 *
 * Because of these reasons, this driver doesn't support Clock/Frame Provider Mode. Use it as
 * Clock/Frame Consumer Mode.
 */

/*
 * [NOTE-RESET]
 *
 * MSIOF has TXRST/RXRST to reset FIFO, but it shouldn't be used during SYNC signal was asserted,
 * because it will be cause of HW issue.
 *
 * When MSIOF is used as Sound driver, this driver is assuming it is used as clock consumer mode
 * (= Codec is clock provider). This means, it can't control SYNC signal by itself.
 *
 * We need to use SW reset (= reset_control_xxx()) instead of TXRST/RXRST.
 */

/*
 * [NOTE-BOTH-SETTING]
 *
 * SITMDRn / SIRMDRn and some other registers should not be updated during working even though it
 * was not related the target direction (for example, do TX settings during RX is working),
 * otherwise it cause a FSERR.
 *
 * Setup both direction (Playback/Capture) in the same time.
 */

/*
 * [NOTE-R/L]
 *
 * The data of Captured might be R/L opposite.
 *
 * This driver is assuming MSIOF is used as Clock/Frame Consumer Mode, and there is a case that some
 * Codec (= Clock/Frame Provider) might output Clock/Frame before setup MSIOF. It depends on Codec
 * driver implementation.
 *
 * MSIOF will capture data without checking SYNC signal Hi/Low (= R/L).
 *
 * This means, if MSIOF RXE bit was set as 1 in case of SYNC signal was Hi (= R) timing, it will
 * start capture data since next SYNC low singla (= L). Because Linux assumes sound data is lined
 * up as R->L->R->L->..., the data R/L will be opposite.
 *
 * The only solution in this case is start CLK/SYNC *after* MSIOF settings, but it depends when and
 * how Codec driver start it.
 */

/*
 * [NOTE-FSERR]
 *
 * We can't remove all FSERR.
 *
 * Renesas have tried to minimize the occurrence of FSERR errors as much as possible, but
 * unfortunately we cannot remove them completely, because MSIOF might setup its register during
 * CLK/SYNC are inputed. It can be happen because MSIOF is working as Clock/Frame Consumer.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/spi/sh_msiof.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>

/* SISTR */
#define SISTR_ERR_TX	(SISTR_TFSERR | SISTR_TFOVF | SISTR_TFUDF)
#define SISTR_ERR_RX	(SISTR_RFSERR | SISTR_RFOVF | SISTR_RFUDF)

/*
 * The data on memory in 24bit case is located at <right> side
 *	[  xxxxxx]
 *	[  xxxxxx]
 *	[  xxxxxx]
 *
 * HW assuming signal in 24bit case is located at <left> side
 *	---+         +---------+
 *	   +---------+         +---------+...
 *	   [xxxxxx  ][xxxxxx  ][xxxxxx  ]
 *
 * When we use 24bit data, it will be transferred via 32bit width via DMA,
 * and MSIOF/DMA doesn't support data shift, we can't use 24bit data correctly.
 * There is no such issue on 16/32bit data case.
 */
#define MSIOF_RATES	SNDRV_PCM_RATE_8000_192000
#define MSIOF_FMTS	(SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

struct msiof_priv {
	struct device *dev;
	struct snd_pcm_substream *substream[SNDRV_PCM_STREAM_LAST + 1];
	struct reset_control *reset;
	spinlock_t lock;
	void __iomem *base;
	resource_size_t phy_addr;

	int count;

	/* for error */
	int err_syc[SNDRV_PCM_STREAM_LAST + 1];
	int err_ovf[SNDRV_PCM_STREAM_LAST + 1];
	int err_udf[SNDRV_PCM_STREAM_LAST + 1];

	/* bit field */
	u32 flags;
#define MSIOF_FLAGS_NEED_DELAY		(1 << 0)
};
#define msiof_flag_has(priv, flag)	(priv->flags &  flag)
#define msiof_flag_set(priv, flag)	(priv->flags |= flag)

#define msiof_is_play(substream)	((substream)->stream == SNDRV_PCM_STREAM_PLAYBACK)
#define msiof_read(priv, reg)		ioread32((priv)->base + reg)
#define msiof_write(priv, reg, val)	iowrite32(val, (priv)->base + reg)

static int msiof_update(struct msiof_priv *priv, u32 reg, u32 mask, u32 val)
{
	u32 old = msiof_read(priv, reg);
	u32 new = (old & ~mask) | (val & mask);
	int updated = false;

	if (old != new) {
		msiof_write(priv, reg, new);
		updated = true;
	}

	return updated;
}

static void msiof_update_and_wait(struct msiof_priv *priv, u32 reg, u32 mask, u32 val, u32 expect)
{
	u32 data;
	int ret;

	ret = msiof_update(priv, reg, mask, val);
	if (!ret) /* no update */
		return;

	ret = readl_poll_timeout_atomic(priv->base + reg, data,
					(data & mask) == expect, 1, 128);
	if (ret)
		dev_warn(priv->dev, "write timeout [0x%02x] 0x%08x / 0x%08x\n",
			 reg, data, expect);
}

static int msiof_hw_start(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream, int cmd)
{
	struct msiof_priv *priv = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int is_play = msiof_is_play(substream);
	int width = snd_pcm_format_width(runtime->format);
	u32 val;

	/*
	 * see
	 *	[NOTE-CLOCK-MODE] on top of this driver
	 */
	/*
	 * see
	 *	Datasheet 109.3.6 [Transmit and Receive Procedures]
	 *
	 *	TX: Fig 109.14	- Fig 109.23
	 *	RX: Fig 109.15
	 */

	/*
	 * Use reset_control_xx() instead of TXRST/RXRST.
	 * see
	 *	[NOTE-RESET]
	 */
	if (!priv->count)
		reset_control_deassert(priv->reset);

	priv->count++;

	/*
	 * Reset errors. ignore 1st FSERR
	 *
	 * see
	 *	[NOTE-FSERR]
	 */
	priv->err_syc[substream->stream] = -1;
	priv->err_ovf[substream->stream] =
	priv->err_udf[substream->stream] = 0;

	/* Start DMAC */
	snd_dmaengine_pcm_trigger(substream, cmd);

	/*
	 * setup both direction (Playback/Capture) in the same time.
	 * see
	 *	above [NOTE-BOTH-SETTING]
	 */

	/* SITMDRx */
	val = SITMDR1_PCON | SIMDR1_SYNCAC | SIMDR1_XXSTP |
		FIELD_PREP(SIMDR1_SYNCMD, SIMDR1_SYNCMD_LR);
	if (msiof_flag_has(priv, MSIOF_FLAGS_NEED_DELAY))
		val |= FIELD_PREP(SIMDR1_DTDL, 1);

	msiof_write(priv, SITMDR1, val);

	val = FIELD_PREP(SIMDR2_BITLEN1, width - 1);
	msiof_write(priv, SITMDR2, val | FIELD_PREP(SIMDR2_GRP, 1));
	msiof_write(priv, SITMDR3, val);

	/* SIRMDRx */
	val = SIMDR1_SYNCAC |
		FIELD_PREP(SIMDR1_SYNCMD, SIMDR1_SYNCMD_LR);
	if (msiof_flag_has(priv, MSIOF_FLAGS_NEED_DELAY))
		val |= FIELD_PREP(SIMDR1_DTDL, 1);

	msiof_write(priv, SIRMDR1, val);

	val = FIELD_PREP(SIMDR2_BITLEN1, width - 1);
	msiof_write(priv, SIRMDR2, val | FIELD_PREP(SIMDR2_GRP, 1));
	msiof_write(priv, SIRMDR3, val);

	/* SIFCTR */
	msiof_write(priv, SIFCTR,
		    FIELD_PREP(SIFCTR_TFWM, SIFCTR_TFWM_1) |
		    FIELD_PREP(SIFCTR_RFWM, SIFCTR_RFWM_1));

	/* SIIER */
	if (is_play)
		val = SIIER_TDREQE | SIIER_TDMAE | SISTR_ERR_TX;
	else
		val = SIIER_RDREQE | SIIER_RDMAE | SISTR_ERR_RX;
	msiof_update(priv, SIIER, val, val);

	/* clear status */
	if (is_play)
		val = SISTR_ERR_TX;
	else
		val = SISTR_ERR_RX;
	msiof_update(priv, SISTR, val, val);

	/* SICTR */
	val = SICTR_TEDG | SICTR_REDG;
	if (is_play)
		val |= SICTR_TXE;
	else
		val |= SICTR_RXE;
	msiof_update_and_wait(priv, SICTR, val, val, val);

	return 0;
}

static int msiof_hw_stop(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream, int cmd)
{
	struct msiof_priv *priv = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	int is_play = msiof_is_play(substream);
	u32 val;

	/* SIIER */
	if (is_play)
		val = SIIER_TDREQE | SIIER_TDMAE | SISTR_ERR_TX;
	else
		val = SIIER_RDREQE | SIIER_RDMAE | SISTR_ERR_RX;
	msiof_update(priv, SIIER, val, 0);

	/* SICTR */
	if (is_play)
		val = SICTR_TXE;
	else
		val = SICTR_RXE;
	msiof_update_and_wait(priv, SICTR, val, 0, 0);

	/* Stop DMAC */
	snd_dmaengine_pcm_trigger(substream, cmd);

	/*
	 * Ignore 1st FSERR
	 *
	 * see
	 *	[NOTE-FSERR]
	 */
	if (priv->err_syc[substream->stream] < 0)
		priv->err_syc[substream->stream] = 0;

	/* indicate error status if exist */
	if (priv->err_syc[substream->stream] ||
	    priv->err_ovf[substream->stream] ||
	    priv->err_udf[substream->stream])
		dev_warn(dev, "%s: FSERR = %d, FOVF = %d, FUDF = %d\n",
			 snd_pcm_direction_name(substream->stream),
			 priv->err_syc[substream->stream],
			 priv->err_ovf[substream->stream],
			 priv->err_udf[substream->stream]);

	priv->count--;

	if (!priv->count)
		reset_control_assert(priv->reset);

	return 0;
}

static int msiof_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct msiof_priv *priv = snd_soc_dai_get_drvdata(dai);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	/*
	 * It supports Clock/Frame Consumer Mode only
	 * see
	 *	[NOTE] on top of this driver
	 */
	case SND_SOC_DAIFMT_BC_FC:
		break;
	/* others are error */
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	/* it supports NB_NF only */
	case SND_SOC_DAIFMT_NB_NF:
	default:
		break;
	/* others are error */
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_NF:
	case SND_SOC_DAIFMT_IB_IF:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		msiof_flag_set(priv, MSIOF_FLAGS_NEED_DELAY);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Select below from Sound Card, not auto
 *	SND_SOC_DAIFMT_CBC_CFC
 *	SND_SOC_DAIFMT_CBP_CFP
 */
static const u64 msiof_dai_formats = SND_SOC_POSSIBLE_DAIFMT_I2S	|
				     SND_SOC_POSSIBLE_DAIFMT_LEFT_J	|
				     SND_SOC_POSSIBLE_DAIFMT_NB_NF;

static const struct snd_soc_dai_ops msiof_dai_ops = {
	.set_fmt			= msiof_dai_set_fmt,
	.auto_selectable_formats	= &msiof_dai_formats,
	.num_auto_selectable_formats	= 1,
};

static struct snd_soc_dai_driver msiof_dai_driver = {
	.name = "msiof-dai",
	.playback = {
		.rates		= MSIOF_RATES,
		.formats	= MSIOF_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.capture = {
		.rates		= MSIOF_RATES,
		.formats	= MSIOF_FMTS,
		.channels_min	= 2,
		.channels_max	= 2,
	},
	.ops = &msiof_dai_ops,
	.symmetric_rate		= 1,
	.symmetric_channels	= 1,
	.symmetric_sample_bits	= 1,
};

static struct snd_pcm_hardware msiof_pcm_hardware = {
	.info =	SNDRV_PCM_INFO_INTERLEAVED	|
		SNDRV_PCM_INFO_MMAP		|
		SNDRV_PCM_INFO_MMAP_VALID,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 64,
};

static int msiof_open(struct snd_soc_component *component,
		      struct snd_pcm_substream *substream)
{
	struct device *dev = component->dev;
	struct dma_chan *chan;
	static const char * const dma_names[] = {"rx", "tx"};
	int is_play = msiof_is_play(substream);
	int ret;

	chan = of_dma_request_slave_channel(dev->of_node, dma_names[is_play]);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	ret = snd_dmaengine_pcm_open(substream, chan);
	if (ret < 0)
		goto open_err_dma;

	snd_soc_set_runtime_hwparams(substream, &msiof_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(substream->runtime, SNDRV_PCM_HW_PARAM_PERIODS);

open_err_dma:
	if (ret < 0)
		dma_release_channel(chan);

	return ret;
}

static int msiof_close(struct snd_soc_component *component,
		       struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_close_release_chan(substream);
}

static snd_pcm_uframes_t msiof_pointer(struct snd_soc_component *component,
				       struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_pointer(substream);
}

#define PREALLOC_BUFFER		(32 * 1024)
#define PREALLOC_BUFFER_MAX	(32 * 1024)
static int msiof_new(struct snd_soc_component *component,
		     struct snd_soc_pcm_runtime *rtd)
{
	snd_pcm_set_managed_buffer_all(rtd->pcm, SNDRV_DMA_TYPE_DEV,
				       rtd->card->snd_card->dev,
				       PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
	return 0;
}

static int msiof_trigger(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream, int cmd)
{
	struct device *dev = component->dev;
	struct msiof_priv *priv = dev_get_drvdata(dev);
	int ret = -EINVAL;

	guard(spinlock_irqsave)(&priv->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		priv->substream[substream->stream] = substream;
		fallthrough;
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = msiof_hw_start(component, substream, cmd);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		priv->substream[substream->stream] = NULL;
		fallthrough;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ret = msiof_hw_stop(component, substream, cmd);
		break;
	}

	return ret;
}

static int msiof_hw_params(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct msiof_priv *priv = dev_get_drvdata(component->dev);
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	struct dma_slave_config cfg = {};
	int ret;

	guard(spinlock_irqsave)(&priv->lock);

	ret = snd_hwparams_to_dma_slave_config(substream, params, &cfg);
	if (ret < 0)
		return ret;

	cfg.dst_addr = priv->phy_addr + SITFDR;
	cfg.src_addr = priv->phy_addr + SIRFDR;

	return dmaengine_slave_config(chan, &cfg);
}

static const struct snd_soc_component_driver msiof_component_driver = {
	.name		= "msiof",
	.open		= msiof_open,
	.close		= msiof_close,
	.pointer	= msiof_pointer,
	.pcm_construct	= msiof_new,
	.trigger	= msiof_trigger,
	.hw_params	= msiof_hw_params,
};

static irqreturn_t msiof_interrupt(int irq, void *data)
{
	struct msiof_priv *priv = data;
	struct snd_pcm_substream *substream;
	u32 sistr;

	scoped_guard(spinlock, &priv->lock) {
		sistr = msiof_read(priv, SISTR);
		msiof_write(priv, SISTR, SISTR_ERR_TX | SISTR_ERR_RX);
	}

	/* overflow/underflow error */
	substream = priv->substream[SNDRV_PCM_STREAM_PLAYBACK];
	if (substream && (sistr & SISTR_ERR_TX)) {
		// snd_pcm_stop_xrun(substream);
		if (sistr & SISTR_TFSERR)
			priv->err_syc[SNDRV_PCM_STREAM_PLAYBACK]++;
		if (sistr & SISTR_TFOVF)
			priv->err_ovf[SNDRV_PCM_STREAM_PLAYBACK]++;
		if (sistr & SISTR_TFUDF)
			priv->err_udf[SNDRV_PCM_STREAM_PLAYBACK]++;
	}

	substream = priv->substream[SNDRV_PCM_STREAM_CAPTURE];
	if (substream && (sistr & SISTR_ERR_RX)) {
		// snd_pcm_stop_xrun(substream);
		if (sistr & SISTR_RFSERR)
			priv->err_syc[SNDRV_PCM_STREAM_CAPTURE]++;
		if (sistr & SISTR_RFOVF)
			priv->err_ovf[SNDRV_PCM_STREAM_CAPTURE]++;
		if (sistr & SISTR_RFUDF)
			priv->err_udf[SNDRV_PCM_STREAM_CAPTURE]++;
	}

	return IRQ_HANDLED;
}

static int msiof_probe(struct platform_device *pdev)
{
	struct msiof_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq, ret;

	/* Check MSIOF as Sound mode or SPI mode */
	struct device_node *port __free(device_node) = of_graph_get_next_port(dev->of_node, NULL);
	if (!port)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	reset_control_assert(priv->reset);

	ret = devm_request_irq(dev, irq, msiof_interrupt, 0, dev_name(dev), priv);
	if (ret)
		return ret;

	priv->dev	= dev;
	priv->phy_addr	= res->start;
	priv->count	= 0;

	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	devm_pm_runtime_enable(dev);

	ret = devm_snd_soc_register_component(dev, &msiof_component_driver,
					      &msiof_dai_driver, 1);

	return ret;
}

static const struct of_device_id msiof_of_match[] = {
	{ .compatible = "renesas,rcar-gen4-msiof", },
	{},
};
MODULE_DEVICE_TABLE(of, msiof_of_match);

static struct platform_driver msiof_driver = {
	.driver	= {
		.name	= "msiof-pcm-audio",
		.of_match_table = msiof_of_match,
	},
	.probe		= msiof_probe,
};
module_platform_driver(msiof_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas R-Car MSIOF I2S audio driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
