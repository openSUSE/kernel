// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

struct simple_amp_data {
	unsigned int supports;
#define SIMPLE_AUDIO_SUPPORT_PGA		BIT(0)
#define SIMPLE_AUDIO_SUPPORT_POWER_SUPPLIES	BIT(1)

	const struct snd_soc_dapm_widget *dapm_widgets;
	unsigned int num_dapm_widgets;
	const struct snd_soc_dapm_route *dapm_routes;
	unsigned int num_dapm_routes;
};

struct simple_amp {
	const struct simple_amp_data *data;
	struct gpio_desc *gpiod_enable;
};

static int simple_amp_power_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct simple_amp *simple_amp = snd_soc_component_get_drvdata(c);
	int val;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = 1;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = 0;
		break;
	default:
		WARN(1, "Unexpected event");
		return -EINVAL;
	}

	gpiod_set_value_cansleep(simple_amp->gpiod_enable, val);

	return 0;
}

static const struct snd_soc_dapm_widget simple_amp_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INL"),
	SND_SOC_DAPM_INPUT("INR"),
	SND_SOC_DAPM_OUT_DRV_E("DRV", SND_SOC_NOPM, 0, 0, NULL, 0, simple_amp_power_event,
			       (SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),
	SND_SOC_DAPM_REGULATOR_SUPPLY("VCC", 20, 0),
};

static const struct snd_soc_dapm_route simple_amp_dapm_routes[] = {
	{ "DRV", NULL, "INL" },
	{ "DRV", NULL, "INR" },
	{ "OUTL", NULL, "VCC" },
	{ "OUTR", NULL, "VCC" },
	{ "OUTL", NULL, "DRV" },
	{ "OUTR", NULL, "DRV" },
};

static const struct snd_soc_dapm_widget simple_amp_mono_pga_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),
	SND_SOC_DAPM_OUTPUT("OUT"),
	SND_SOC_DAPM_PGA_E("PGA", SND_SOC_NOPM, 0, 0, NULL, 0, simple_amp_power_event,
			   (SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_REGULATOR_SUPPLY("vdd", 0, 0),
};

static const struct snd_soc_dapm_route simple_amp_mono_pga_dapm_routes[] = {
	{ "PGA", NULL, "IN" },
	{ "PGA", NULL, "vdd" },
	{ "OUT", NULL, "PGA" },
};

static const struct snd_soc_dapm_widget simple_amp_stereo_pga_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INL"),
	SND_SOC_DAPM_INPUT("INR"),
	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),
	SND_SOC_DAPM_PGA_E("PGA", SND_SOC_NOPM, 0, 0, NULL, 0, simple_amp_power_event,
			   (SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_REGULATOR_SUPPLY("vdd", 0, 0),
};

static const struct snd_soc_dapm_route simple_amp_stereo_pga_dapm_routes[] = {
	{ "PGA", NULL, "INL" },
	{ "PGA", NULL, "INR" },
	{ "PGA", NULL, "vdd" },
	{ "OUTL", NULL, "PGA" },
	{ "OUTR", NULL, "PGA" },
};

static int simple_amp_add_basic_dapm(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_to_dapm(component);
	struct simple_amp *simple_amp = snd_soc_component_get_drvdata(component);
	struct device *dev = component->dev;
	int ret;

	/* Add basic dapm widgets and routes */
	ret = snd_soc_dapm_new_controls(dapm, simple_amp->data->dapm_widgets,
					simple_amp->data->num_dapm_widgets);
	if (ret) {
		dev_err(dev, "Failed to add basic dapm widgets (%d)\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(dapm, simple_amp->data->dapm_routes,
				      simple_amp->data->num_dapm_routes);
	if (ret) {
		dev_err(dev, "Failed to add basic dapm routes (%d)\n", ret);
		return ret;
	}

	return 0;
}

struct simple_amp_supply {
	const char *prop_name;
	const struct snd_soc_dapm_widget dapm_widget;
	const struct snd_soc_dapm_route dapm_route;
};

static const struct simple_amp_supply simple_amp_supplies[] = {
	{
		.prop_name = "vddio-supply",
		.dapm_widget = SND_SOC_DAPM_REGULATOR_SUPPLY("vddio", 0, 0),
		.dapm_route = { "PGA", NULL, "vddio" },
	}, {
		.prop_name = "vdda1-supply",
		.dapm_widget = SND_SOC_DAPM_REGULATOR_SUPPLY("vdda1", 0, 0),
		.dapm_route = { "PGA", NULL, "vdda1" },
	}, {
		.prop_name = "vdda2-supply",
		.dapm_widget = SND_SOC_DAPM_REGULATOR_SUPPLY("vdda2", 0, 0),
		.dapm_route = { "PGA", NULL, "vdda2" },
	},
	{ /* End of list */}
};

static int simple_amp_add_power_supplies(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_to_dapm(component);
	struct simple_amp *simple_amp = snd_soc_component_get_drvdata(component);
	const struct simple_amp_supply *supply;
	struct device *dev = component->dev;
	int ret;

	/*
	 * Those additional power supplies are attached to the PGA.
	 * If PGA is not supported, simply skipped them.
	 */
	if (!(simple_amp->data->supports & SIMPLE_AUDIO_SUPPORT_PGA)) {
		dev_err(dev, "Extra power supplied need PGA\n");
		return -EINVAL;
	}

	supply = simple_amp_supplies;
	do {
		if (!of_property_present(dev->of_node, supply->prop_name))
			continue;

		ret = snd_soc_dapm_new_controls(dapm, &supply->dapm_widget, 1);
		if (ret) {
			dev_err(dev, "Failed to add control for '%s' (%d)\n",
				supply->prop_name, ret);
			return ret;
		}
		ret = snd_soc_dapm_add_routes(dapm, &supply->dapm_route, 1);
		if (ret) {
			dev_err(dev, "Failed to add route for '%s' (%d)\n",
				supply->prop_name, ret);
			return ret;
		}
	} while ((++supply)->prop_name);

	return 0;
}

static int simple_amp_component_probe(struct snd_soc_component *component)
{
	struct simple_amp *simple_amp = snd_soc_component_get_drvdata(component);
	int ret;

	/* Add basic dapm widgets and routes */
	ret = simple_amp_add_basic_dapm(component);
	if (ret)
		return ret;

	/* Add additional power supplies */
	if (simple_amp->data->supports & SIMPLE_AUDIO_SUPPORT_POWER_SUPPLIES) {
		ret = simple_amp_add_power_supplies(component);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct snd_soc_component_driver simple_amp_component_driver = {
	.probe = simple_amp_component_probe,
};

static int simple_amp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simple_amp *simple_amp;

	simple_amp = devm_kzalloc(dev, sizeof(*simple_amp), GFP_KERNEL);
	if (!simple_amp)
		return -ENOMEM;
	platform_set_drvdata(pdev, simple_amp);

	simple_amp->data = of_device_get_match_data(dev);
	if (!simple_amp->data)
		return -EINVAL;

	simple_amp->gpiod_enable = devm_gpiod_get_optional(dev, "enable",
							   GPIOD_OUT_LOW);
	if (IS_ERR(simple_amp->gpiod_enable))
		return dev_err_probe(dev, PTR_ERR(simple_amp->gpiod_enable),
				     "Failed to get 'enable' gpio");

	return devm_snd_soc_register_component(dev,
					       &simple_amp_component_driver,
					       NULL, 0);
}

static const struct simple_amp_data simple_audio_amplifier_data = {
	.dapm_widgets		= simple_amp_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(simple_amp_dapm_widgets),
	.dapm_routes		= simple_amp_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(simple_amp_dapm_routes),
};

static const struct simple_amp_data simple_audio_mono_pga_data = {
	.supports		= SIMPLE_AUDIO_SUPPORT_PGA |
				  SIMPLE_AUDIO_SUPPORT_POWER_SUPPLIES,
	.dapm_widgets		= simple_amp_mono_pga_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(simple_amp_mono_pga_dapm_widgets),
	.dapm_routes		= simple_amp_mono_pga_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(simple_amp_mono_pga_dapm_routes),
};

static const struct simple_amp_data simple_audio_stereo_pga_data = {
	.supports		= SIMPLE_AUDIO_SUPPORT_PGA |
				  SIMPLE_AUDIO_SUPPORT_POWER_SUPPLIES,
	.dapm_widgets		= simple_amp_stereo_pga_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(simple_amp_stereo_pga_dapm_widgets),
	.dapm_routes		= simple_amp_stereo_pga_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(simple_amp_stereo_pga_dapm_routes),
};

static const struct of_device_id simple_amp_ids[] = {
	{ .compatible = "dioo,dio2125",		  .data = &simple_audio_amplifier_data},
	{ .compatible = "simple-audio-amplifier", .data = &simple_audio_amplifier_data},
	{ .compatible = "gpio-audio-amp-mono",	  .data = &simple_audio_mono_pga_data},
	{ .compatible = "gpio-audio-amp-stereo",  .data = &simple_audio_stereo_pga_data},
	{ }
};
MODULE_DEVICE_TABLE(of, simple_amp_ids);

static struct platform_driver simple_amp_driver = {
	.driver = {
		.name = "simple-amplifier",
		.of_match_table = simple_amp_ids,
	},
	.probe = simple_amp_probe,
};

module_platform_driver(simple_amp_driver);

MODULE_DESCRIPTION("ASoC Simple Audio Amplifier driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
