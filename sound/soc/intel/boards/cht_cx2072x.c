/*
 *  cht_cx207x.c - ASoc DPCM Machine driver for CherryTrail w/ CX2072x
 *
 *  Copyright (C) 2016 Intel Corp
 *  Author: Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../../codecs/cx2072x.h"
#include "../atom/sst-atom-controls.h"
#include "../common/sst-acpi.h"

static const struct snd_soc_dapm_widget cht_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route cht_audio_map[] = {
	/* External Speakers: HFL, HFR */
	{"Headphone", NULL, "PORTA"},
	{"Ext Spk", NULL, "PORTG"},
	{"PORTC", NULL, "Int Mic"},
	{"PORTD", NULL, "Headset Mic"},

	{"Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
	{"ssp2 Rx", NULL, "Capture"},
	{"ssp0 Tx", NULL, "modem_out"},
	{"modem_in", NULL, "ssp0 Rx"},
};

static const struct snd_kcontrol_new cht_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static int cht_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	return 0;
}

static struct snd_soc_jack cht_cx_headset;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin cht_cx_headset_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
};

static const struct acpi_gpio_params headset_gpios = { 0, 0, false };

static const struct acpi_gpio_mapping acpi_cht_cx2072x_gpios[] = {
	{ "headset-gpios", &headset_gpios, 1 },
	{},
};

static int cht_cx_jack_status_check(void *data)
{
	return cx2072x_get_jack_state(data);
}

static struct snd_soc_jack_gpio cht_cx_gpio = {
	.name = "headset",
	.report = SND_JACK_HEADSET | SND_JACK_BTN_0,
	.debounce_time = 150,
	.wake = true,
	.jack_status_check = cht_cx_jack_status_check,
};

static int cht_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_codec *codec = rtd->codec;

	if (devm_acpi_dev_add_driver_gpios(codec->dev,
					   acpi_cht_cx2072x_gpios))
		dev_warn(rtd->dev, "Unable to add GPIO mapping table\n");

	card->dapm.idle_bias_off = true;

	/* set the default PLL rate, the clock is handled by the codec driver */
	ret = snd_soc_dai_set_sysclk(rtd->codec_dai, CX2072X_MCLK_EXTERNAL_PLL,
				     19200000, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(rtd->dev, "Could not set sysclk\n");
		return ret;
	}

	ret = snd_soc_card_jack_new(card, "Headset",
				    SND_JACK_HEADSET | SND_JACK_BTN_0,
				    &cht_cx_headset,
				    cht_cx_headset_pins,
				    ARRAY_SIZE(cht_cx_headset_pins));
	if (ret)
		return ret;

	cht_cx_gpio.gpiod_dev = codec->dev;
	cht_cx_gpio.data = codec;
	ret = snd_soc_jack_add_gpios(&cht_cx_headset, 1, &cht_cx_gpio);
	if (ret) {
		dev_err(rtd->dev, "Adding jack GPIO failed\n");
		return ret;
	}

	cx2072x_enable_detect(codec);

	return ret;
}

static const struct snd_soc_pcm_stream cht_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int cht_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret;

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch 24-bit. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(rtd->cpu_dai,
				SND_SOC_DAIFMT_I2S     |
				SND_SOC_DAIFMT_NB_NF   |
				SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	snd_soc_dai_set_bclk_ratio(rtd->codec_dai, 50);
	return 0;
}

static const struct snd_soc_pcm_stream cht_cx_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int cht_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
					SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static struct snd_soc_ops cht_aif1_ops = {
	.startup = cht_aif1_startup,
};

static struct snd_soc_ops cht_be_ssp2_ops = {
	.hw_params = cht_aif1_hw_params,
};

static struct snd_soc_dai_link cht_dailink[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cht_aif1_ops,
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &cht_aif1_ops,
	},
	[MERR_DPCM_COMPR] = {
		.name = "Compressed Port",
		.stream_name = "Compress",
		.cpu_dai_name = "compress-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
	},
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 1,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "cx2072x-hifi",
		.codec_name = "i2c-14F10720:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
					      | SND_SOC_DAIFMT_CBS_CFS,
		.init = cht_codec_init,
		.be_hw_params_fixup = cht_codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cht_be_ssp2_ops,
	},
};

/* SoC card */
static struct snd_soc_card chtcx2072x_card = {
	.name = "chtcx2072x",
	.dai_link = cht_dailink,
	.num_links = ARRAY_SIZE(cht_dailink),
	.dapm_widgets = cht_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cht_dapm_widgets),
	.dapm_routes = cht_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cht_audio_map),
	.controls = cht_mc_controls,
	.num_controls = ARRAY_SIZE(cht_mc_controls),
};

static char cht_cx_codec_name[16]; /* i2c-<HID>:00 with HID being 8 chars */

static int snd_cht_mc_probe(struct platform_device *pdev)
{
	int i;
	int dai_index;
	struct sst_acpi_mach *mach;
	const char *i2c_name = NULL;

	/* register the soc card */
	chtcx2072x_card.dev = &pdev->dev;
	mach = chtcx2072x_card.dev->platform_data;

	/* fix index of codec dai */
	dai_index = MERR_DPCM_COMPR + 1;
	for (i = 0; i < ARRAY_SIZE(cht_dailink); i++) {
		if (!strcmp(cht_dailink[i].codec_name, "i2c-14F10720:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = sst_acpi_find_name_from_hid(mach->id);
	if (i2c_name != NULL) {
		snprintf(cht_cx_codec_name, sizeof(cht_cx_codec_name),
			"%s%s", "i2c-", i2c_name);
		cht_dailink[dai_index].codec_name = cht_cx_codec_name;
	}

	return devm_snd_soc_register_card(&pdev->dev, &chtcx2072x_card);
}

static int snd_cht_mc_remove(struct platform_device *pdev)
{
	snd_soc_jack_free_gpios(&cht_cx_headset, 1, &cht_cx_gpio);
	return 0;
}

static struct platform_driver snd_cht_mc_driver = {
	.driver = {
		.name = "cht-cx2072x",
	},
	.probe = snd_cht_mc_probe,
	.remove = snd_cht_mc_remove,
};
module_platform_driver(snd_cht_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Cherrytrail Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cht-cx2072x");
