// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for OneXPlayer gamepad configuration devices.
 *
 *  Copyright (c) 2026 Valve Corporation
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/hid.h>
#include <linux/jiffies.h>
#include <linux/kstrtox.h>
#include <linux/led-class-multicolor.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "hid-ids.h"

#define OXP_PACKET_SIZE 64

#define GEN1_MESSAGE_ID	0xff
#define GEN2_MESSAGE_ID	0x3f

#define GEN1_USAGE_PAGE	0xff01
#define GEN2_USAGE_PAGE	0xff00

enum oxp_function_index {
	OXP_FID_GEN1_RGB_SET =		0x07,
	OXP_FID_GEN1_RGB_REPLY =	0x0f,
	OXP_FID_GEN2_STATUS_EVENT =	0xb8,
};

static struct oxp_hid_cfg {
	struct delayed_work oxp_rgb_queue;
	struct led_classdev_mc *led_mc;
	struct hid_device *hdev;
	struct mutex cfg_mutex; /*ensure single synchronous output report*/
	u8 rgb_brightness;
	u8 rgb_effect;
	u8 rgb_speed;
	u8 rgb_en;
} drvdata;

enum oxp_feature_en_index {
	OXP_FEAT_DISABLED,
	OXP_FEAT_ENABLED,
};

static const char *const oxp_feature_en_text[] = {
	[OXP_FEAT_DISABLED] = "false",
	[OXP_FEAT_ENABLED] = "true",
};

enum oxp_rgb_effect_index {
	OXP_UNKNOWN,
	OXP_EFFECT_AURORA,
	OXP_EFFECT_BIRTHDAY,
	OXP_EFFECT_FLOWING,
	OXP_EFFECT_CHROMA_1,
	OXP_EFFECT_NEON,
	OXP_EFFECT_CHROMA_2,
	OXP_EFFECT_DREAMY,
	OXP_EFFECT_WARM,
	OXP_EFFECT_CYBERPUNK,
	OXP_EFFECT_SEA,
	OXP_EFFECT_SUNSET,
	OXP_EFFECT_COLORFUL,
	OXP_EFFECT_MONSTER,
	OXP_EFFECT_GREEN,
	OXP_EFFECT_BLUE,
	OXP_EFFECT_YELLOW,
	OXP_EFFECT_TEAL,
	OXP_EFFECT_PURPLE,
	OXP_EFFECT_FOGGY,
	OXP_EFFECT_MONO_LIST, /* placeholder for effect_index_show */
};

/* These belong to rgb_effect_index, but we want to hide them from
 * rgb_effect_text
 */

#define OXP_GET_PROPERTY 0xfc
#define OXP_SET_PROPERTY 0xfd
#define OXP_EFFECT_MONO_TRUE 0xfe /* actual index for monocolor */

static const char *const oxp_rgb_effect_text[] = {
	[OXP_UNKNOWN] = "unknown",
	[OXP_EFFECT_AURORA] = "aurora",
	[OXP_EFFECT_BIRTHDAY] = "birthday_cake",
	[OXP_EFFECT_FLOWING] = "flowing_light",
	[OXP_EFFECT_CHROMA_1] = "chroma_popping",
	[OXP_EFFECT_NEON] = "neon",
	[OXP_EFFECT_CHROMA_2] = "chroma_breathing",
	[OXP_EFFECT_DREAMY] = "dreamy",
	[OXP_EFFECT_WARM] = "warm_sun",
	[OXP_EFFECT_CYBERPUNK] = "cyberpunk",
	[OXP_EFFECT_SEA] = "sea_foam",
	[OXP_EFFECT_SUNSET] = "sunset_afterglow",
	[OXP_EFFECT_COLORFUL] = "colorful",
	[OXP_EFFECT_MONSTER] = "monster_woke",
	[OXP_EFFECT_GREEN] = "green_breathing",
	[OXP_EFFECT_BLUE] = "blue_breathing",
	[OXP_EFFECT_YELLOW] = "yellow_breathing",
	[OXP_EFFECT_TEAL] = "teal_breathing",
	[OXP_EFFECT_PURPLE] = "purple_breathing",
	[OXP_EFFECT_FOGGY] = "foggy_haze",
	[OXP_EFFECT_MONO_LIST] = "monocolor",
};

struct oxp_gen_1_rgb_report {
	u8 report_id;
	u8 message_id;
	u8 padding_2[2];
	u8 effect;
	u8 enabled;
	u8 speed;
	u8 brightness;
	u8 red;
	u8 green;
	u8 blue;
} __packed;

struct oxp_gen_2_rgb_report {
	u8 report_id;
	u8 header_id;
	u8 padding_2;
	u8 message_id;
	u8 padding_4[2];
	u8 enabled;
	u8 speed;
	u8 brightness;
	u8 red;
	u8 green;
	u8 blue;
	u8 padding_12[3];
	u8 effect;
} __packed;

static u16 get_usage_page(struct hid_device *hdev)
{
	return hdev->collection[0].usage >> 16;
}

static int oxp_hid_raw_event_gen_1(struct hid_device *hdev,
				   struct hid_report *report, u8 *data,
				   int size)
{
	struct led_classdev_mc *led_mc = drvdata.led_mc;
	struct oxp_gen_1_rgb_report *rgb_rep;

	if (data[1] != OXP_FID_GEN1_RGB_REPLY)
		return 0;

	rgb_rep = (struct oxp_gen_1_rgb_report *)data;
	/* Ensure we save monocolor as the list value */
	drvdata.rgb_effect = rgb_rep->effect == OXP_EFFECT_MONO_TRUE ?
			     OXP_EFFECT_MONO_LIST :
			     rgb_rep->effect;
	drvdata.rgb_speed = rgb_rep->speed;
	drvdata.rgb_en = rgb_rep->enabled == 0 ? OXP_FEAT_DISABLED :
						 OXP_FEAT_ENABLED;
	drvdata.rgb_brightness = rgb_rep->brightness;
	led_mc->led_cdev.brightness = rgb_rep->brightness / 4 *
				      led_mc->led_cdev.max_brightness;
	/* If monocolor had less than 100% brightness on the previous boot,
	 * there will be no reliable way to determine the real intensity.
	 * Since intensity scaling is used with a hardware brightness set at max,
	 * our brightness will always look like 100%. Use the last set value to
	 * prevent successive boots from lowering the brightness further.
	 * Brightness will be "wrong" but the effect will remain the same visually.
	 */
	led_mc->subled_info[0].intensity = rgb_rep->red;
	led_mc->subled_info[1].intensity = rgb_rep->green;
	led_mc->subled_info[2].intensity = rgb_rep->blue;

	return 0;
}

static int oxp_hid_raw_event_gen_2(struct hid_device *hdev,
				   struct hid_report *report, u8 *data,
				   int size)
{
	struct led_classdev_mc *led_mc = drvdata.led_mc;
	struct oxp_gen_2_rgb_report *rgb_rep;

	if (data[0] != OXP_FID_GEN2_STATUS_EVENT)
		return 0;

	if (data[3] != OXP_GET_PROPERTY)
		return 0;

	rgb_rep = (struct oxp_gen_2_rgb_report *)data;
	/* Ensure we save monocolor as the list value */
	drvdata.rgb_effect = rgb_rep->effect == OXP_EFFECT_MONO_TRUE ?
			     OXP_EFFECT_MONO_LIST :
			     rgb_rep->effect;
	drvdata.rgb_speed = rgb_rep->speed;
	drvdata.rgb_en = rgb_rep->enabled == 0 ? OXP_FEAT_DISABLED :
						 OXP_FEAT_ENABLED;
	drvdata.rgb_brightness = rgb_rep->brightness;
	led_mc->led_cdev.brightness = rgb_rep->brightness / 4 *
				      led_mc->led_cdev.max_brightness;
	/* If monocolor had less than 100% brightness on the previous boot,
	 * there will be no reliable way to determine the real intensity.
	 * Since intensity scaling is used with a hardware brightness set at max,
	 * our brightness will always look like 100%. Use the last set value to
	 * prevent successive boots from lowering the brightness further.
	 * Brightness will be "wrong" but the effect will remain the same visually.
	 */
	led_mc->subled_info[0].intensity = rgb_rep->red;
	led_mc->subled_info[1].intensity = rgb_rep->green;
	led_mc->subled_info[2].intensity = rgb_rep->blue;

	return 0;
}

static int oxp_hid_raw_event(struct hid_device *hdev, struct hid_report *report,
			     u8 *data, int size)
{
	u16 up = get_usage_page(hdev);

	dev_dbg(&hdev->dev, "raw event data: [%*ph]\n", OXP_PACKET_SIZE, data);

	switch (up) {
	case GEN1_USAGE_PAGE:
		return oxp_hid_raw_event_gen_1(hdev, report, data, size);
	case GEN2_USAGE_PAGE:
		return oxp_hid_raw_event_gen_2(hdev, report, data, size);
	default:
		break;
	}

	return 0;
}

static int mcu_property_out(u8 *header, size_t header_size, u8 *data,
			    size_t data_size, u8 *footer, size_t footer_size)
{
	unsigned char *dmabuf __free(kfree) = kzalloc(OXP_PACKET_SIZE, GFP_KERNEL);
	int ret;

	if (!dmabuf)
		return -ENOMEM;

	if (header_size + data_size + footer_size > OXP_PACKET_SIZE)
		return -EINVAL;

	guard(mutex)(&drvdata.cfg_mutex);
	memcpy(dmabuf, header, header_size);
	memcpy(dmabuf + header_size, data, data_size);
	if (footer_size)
		memcpy(dmabuf + OXP_PACKET_SIZE - footer_size, footer, footer_size);

	dev_dbg(&drvdata.hdev->dev, "raw data: [%*ph]\n", OXP_PACKET_SIZE, dmabuf);

	ret = hid_hw_output_report(drvdata.hdev, dmabuf, OXP_PACKET_SIZE);
	if (ret < 0)
		return ret;

	/* MCU takes 200ms to be ready for another command. */
	msleep(200);
	return ret == OXP_PACKET_SIZE ? 0 : -EIO;
}

static int oxp_gen_1_property_out(enum oxp_function_index fid, u8 *data,
				  u8 data_size)
{
	u8 header[] = { fid, GEN1_MESSAGE_ID };
	size_t header_size = ARRAY_SIZE(header);

	return mcu_property_out(header, header_size, data, data_size, NULL, 0);
}

static int oxp_gen_2_property_out(enum oxp_function_index fid, u8 *data,
				  u8 data_size)
{
	u8 header[] = { fid, GEN2_MESSAGE_ID, 0x01 };
	u8 footer[] = { GEN2_MESSAGE_ID, fid };
	size_t header_size = ARRAY_SIZE(header);
	size_t footer_size = ARRAY_SIZE(footer);

	return mcu_property_out(header, header_size, data, data_size, footer,
				footer_size);
}

static int oxp_rgb_status_store(u8 enabled, u8 speed, u8 brightness)
{
	u16 up = get_usage_page(drvdata.hdev);
	u8 *data;

	/* Always default to max brightness and use intensity scaling when in
	 * monocolor mode.
	 */
	switch (up) {
	case GEN1_USAGE_PAGE:
		data = (u8[4]) { OXP_SET_PROPERTY, enabled, speed, brightness };
		if (drvdata.rgb_effect == OXP_EFFECT_MONO_LIST)
			data[3] = 0x04;
		return oxp_gen_1_property_out(OXP_FID_GEN1_RGB_SET, data, 4);
	case GEN2_USAGE_PAGE:
		data = (u8[6]) { OXP_SET_PROPERTY, 0x00, 0x02, enabled, speed, brightness };
		if (drvdata.rgb_effect == OXP_EFFECT_MONO_LIST)
			data[5] = 0x04;
		return oxp_gen_2_property_out(OXP_FID_GEN2_STATUS_EVENT, data, 6);
	default:
		return -ENODEV;
	}
}

static ssize_t oxp_rgb_status_show(void)
{
	u16 up = get_usage_page(drvdata.hdev);
	u8 *data;

	switch (up) {
	case GEN1_USAGE_PAGE:
		data = (u8[1]) { OXP_GET_PROPERTY };
		return oxp_gen_1_property_out(OXP_FID_GEN1_RGB_SET, data, 1);
	case GEN2_USAGE_PAGE:
		data = (u8[3]) { OXP_GET_PROPERTY, 0x00, 0x02 };
		return oxp_gen_2_property_out(OXP_FID_GEN2_STATUS_EVENT, data, 3);
	default:
		return -ENODEV;
	}
}

static int oxp_rgb_color_set(void)
{
	u8 max_br = drvdata.led_mc->led_cdev.max_brightness;
	u8 br = drvdata.led_mc->led_cdev.brightness;
	u16 up = get_usage_page(drvdata.hdev);
	u8 green, red, blue;
	size_t size;
	u8 *data;
	int i;

	red = br * drvdata.led_mc->subled_info[0].intensity / max_br;
	green = br * drvdata.led_mc->subled_info[1].intensity / max_br;
	blue = br * drvdata.led_mc->subled_info[2].intensity / max_br;

	switch (up) {
	case GEN1_USAGE_PAGE:
		size = 55;
		data = (u8[55]) { OXP_EFFECT_MONO_TRUE };

		for (i = 0; i < (size - 1) / 3; i++) {
			data[3 * i + 1] = red;
			data[3 * i + 2] = green;
			data[3 * i + 3] = blue;
		}
		return oxp_gen_1_property_out(OXP_FID_GEN1_RGB_SET, data, size);
	case GEN2_USAGE_PAGE:
		size = 57;
		data = (u8[57]) { OXP_EFFECT_MONO_TRUE, 0x00, 0x02 };

		for (i = 1; i < size / 3; i++) {
			data[3 * i] = red;
			data[3 * i + 1] = green;
			data[3 * i + 2] = blue;
		}
		return oxp_gen_2_property_out(OXP_FID_GEN2_STATUS_EVENT, data, size);
	default:
		return -ENODEV;
	}
}

static int oxp_rgb_effect_set(u8 effect)
{
	u16 up = get_usage_page(drvdata.hdev);
	u8 *data;
	int ret;

	switch (effect) {
	case OXP_EFFECT_AURORA:
	case OXP_EFFECT_BIRTHDAY:
	case OXP_EFFECT_FLOWING:
	case OXP_EFFECT_CHROMA_1:
	case OXP_EFFECT_NEON:
	case OXP_EFFECT_CHROMA_2:
	case OXP_EFFECT_DREAMY:
	case OXP_EFFECT_WARM:
	case OXP_EFFECT_CYBERPUNK:
	case OXP_EFFECT_SEA:
	case OXP_EFFECT_SUNSET:
	case OXP_EFFECT_COLORFUL:
	case OXP_EFFECT_MONSTER:
	case OXP_EFFECT_GREEN:
	case OXP_EFFECT_BLUE:
	case OXP_EFFECT_YELLOW:
	case OXP_EFFECT_TEAL:
	case OXP_EFFECT_PURPLE:
	case OXP_EFFECT_FOGGY:
		switch (up) {
		case GEN1_USAGE_PAGE:
			data = (u8[1]) { effect };
			ret = oxp_gen_1_property_out(OXP_FID_GEN1_RGB_SET, data, 1);
			break;
		case GEN2_USAGE_PAGE:
			data = (u8[3]) { effect, 0x00, 0x02 };
			ret = oxp_gen_2_property_out(OXP_FID_GEN2_STATUS_EVENT, data, 3);
			break;
		default:
			ret = -ENODEV;
		}
		break;
	case OXP_EFFECT_MONO_LIST:
		ret = oxp_rgb_color_set();
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		return ret;

	drvdata.rgb_effect = effect;

	return 0;
}

static ssize_t enabled_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int ret;
	u8 val;

	ret = sysfs_match_string(oxp_feature_en_text, buf);
	if (ret < 0)
		return ret;
	val = ret;

	ret = oxp_rgb_status_store(val, drvdata.rgb_speed,
				   drvdata.rgb_brightness);
	if (ret)
		return ret;

	drvdata.rgb_en = val;
	return count;
}

static ssize_t enabled_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int ret;

	ret = oxp_rgb_status_show();
	if (ret)
		return ret;

	if (drvdata.rgb_en >= ARRAY_SIZE(oxp_feature_en_text))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", oxp_feature_en_text[drvdata.rgb_en]);
}
static DEVICE_ATTR_RW(enabled);

static ssize_t enabled_index_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(oxp_feature_en_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", oxp_feature_en_text[i]);

	if (count)
		buf[count - 1] = '\n';

	return count;
}
static DEVICE_ATTR_RO(enabled_index);

static ssize_t effect_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int ret;
	u8 val;

	ret = sysfs_match_string(oxp_rgb_effect_text, buf);
	if (ret < 0)
		return ret;

	val = ret;

	ret = oxp_rgb_status_store(drvdata.rgb_en, drvdata.rgb_speed,
				   drvdata.rgb_brightness);
	if (ret)
		return ret;

	ret = oxp_rgb_effect_set(val);
	if (ret)
		return ret;

	return count;
}

static ssize_t effect_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int ret;

	ret = oxp_rgb_status_show();
	if (ret)
		return ret;

	if (drvdata.rgb_effect >= ARRAY_SIZE(oxp_rgb_effect_text))
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", oxp_rgb_effect_text[drvdata.rgb_effect]);
}

static DEVICE_ATTR_RW(effect);

static ssize_t effect_index_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	size_t count = 0;
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(oxp_rgb_effect_text); i++)
		count += sysfs_emit_at(buf, count, "%s ", oxp_rgb_effect_text[i]);

	if (count)
		buf[count - 1] = '\n';

	return count;
}
static DEVICE_ATTR_RO(effect_index);

static ssize_t speed_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int ret;
	u8 val;

	ret = kstrtou8(buf, 10, &val);
	if (ret)
		return ret;

	if (val > 9)
		return -EINVAL;

	ret = oxp_rgb_status_store(drvdata.rgb_en, val, drvdata.rgb_brightness);
	if (ret)
		return ret;

	drvdata.rgb_speed = val;
	return count;
}

static ssize_t speed_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int ret;

	ret = oxp_rgb_status_show();
	if (ret)
		return ret;

	if (drvdata.rgb_speed > 9)
		return -EINVAL;

	return sysfs_emit(buf, "%hhu\n", drvdata.rgb_speed);
}
static DEVICE_ATTR_RW(speed);

static ssize_t speed_range_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "0-9\n");
}
static DEVICE_ATTR_RO(speed_range);

static void oxp_rgb_queue_fn(struct work_struct *work)
{
	unsigned int max_brightness = drvdata.led_mc->led_cdev.max_brightness;
	unsigned int brightness = drvdata.led_mc->led_cdev.brightness;
	u8 val = 4 * brightness / max_brightness;
	int ret;

	if (drvdata.rgb_brightness != val) {
		ret = oxp_rgb_status_store(drvdata.rgb_en, drvdata.rgb_speed, val);
		if (ret)
			dev_err(drvdata.led_mc->led_cdev.dev,
				"Error: Failed to write RGB Status: %i\n", ret);

		drvdata.rgb_brightness = val;
	}

	if (drvdata.rgb_effect != OXP_EFFECT_MONO_LIST)
		return;

	ret = oxp_rgb_effect_set(drvdata.rgb_effect);
	if (ret)
		dev_err(drvdata.led_mc->led_cdev.dev, "Error: Failed to write RGB color: %i\n",
			ret);
}

static void oxp_rgb_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	led_cdev->brightness = brightness;
	mod_delayed_work(system_wq, &drvdata.oxp_rgb_queue, msecs_to_jiffies(50));
}

static struct attribute *oxp_rgb_attrs[] = {
	&dev_attr_effect.attr,
	&dev_attr_effect_index.attr,
	&dev_attr_enabled.attr,
	&dev_attr_enabled_index.attr,
	&dev_attr_speed.attr,
	&dev_attr_speed_range.attr,
	NULL,
};

static const struct attribute_group oxp_rgb_attr_group = {
	.attrs = oxp_rgb_attrs,
};

static struct mc_subled oxp_rgb_subled_info[] = {
	{
		.color_index = LED_COLOR_ID_RED,
		.intensity = 0x24,
		.channel = 0x1,
	},
	{
		.color_index = LED_COLOR_ID_GREEN,
		.intensity = 0x22,
		.channel = 0x2,
	},
	{
		.color_index = LED_COLOR_ID_BLUE,
		.intensity = 0x99,
		.channel = 0x3,
	},
};

static struct led_classdev_mc oxp_cdev_rgb = {
	.led_cdev = {
		.name = "oxp:rgb:joystick_rings",
		.color = LED_COLOR_ID_RGB,
		.brightness = 0x64,
		.max_brightness = 0x64,
		.brightness_set = oxp_rgb_brightness_set,
	},
	.num_colors = ARRAY_SIZE(oxp_rgb_subled_info),
	.subled_info = oxp_rgb_subled_info,
};

struct quirk_entry {
	bool hybrid_mcu;
};

static struct quirk_entry quirk_hybrid_mcu = {
	.hybrid_mcu = true,
};

static const struct dmi_system_id oxp_hybrid_mcu_list[] = {
	{
		.ident = "OneXPlayer Apex",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ONE-NETBOOK"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ONEXPLAYER APEX"),
		},
		.driver_data = &quirk_hybrid_mcu,
	},
	{
		.ident = "OneXPlayer G1 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ONE-NETBOOK"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ONEXPLAYER G1 A"),
		},
		.driver_data = &quirk_hybrid_mcu,
	},
	{
		.ident = "OneXPlayer G1 Intel",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ONE-NETBOOK"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ONEXPLAYER G1 i"),
		},
		.driver_data = &quirk_hybrid_mcu,
	},
	{},
};

static bool oxp_hybrid_mcu_device(void)
{
	const struct dmi_system_id *dmi_id;
	struct quirk_entry *quirks;

	dmi_id = dmi_first_match(oxp_hybrid_mcu_list);
	if (!dmi_id)
		return false;

	quirks = dmi_id->driver_data;

	return quirks->hybrid_mcu;
}

static int oxp_cfg_probe(struct hid_device *hdev, u16 up)
{
	int ret;

	hid_set_drvdata(hdev, &drvdata);
	mutex_init(&drvdata.cfg_mutex);
	drvdata.hdev = hdev;

	if (up == GEN2_USAGE_PAGE && oxp_hybrid_mcu_device())
		goto skip_rgb;

	drvdata.led_mc = &oxp_cdev_rgb;

	INIT_DELAYED_WORK(&drvdata.oxp_rgb_queue, oxp_rgb_queue_fn);
	ret = devm_led_classdev_multicolor_register(&hdev->dev, &oxp_cdev_rgb);
	if (ret)
		return dev_err_probe(&hdev->dev, ret,
				     "Failed to create RGB device\n");

	ret = devm_device_add_group(drvdata.led_mc->led_cdev.dev,
				    &oxp_rgb_attr_group);
	if (ret)
		return dev_err_probe(drvdata.led_mc->led_cdev.dev, ret,
				     "Failed to create RGB configuration attributes\n");

	ret = oxp_rgb_status_show();
	if (ret)
		dev_warn(drvdata.led_mc->led_cdev.dev,
			 "Failed to query RGB initial state: %i\n", ret);

skip_rgb:
	return 0;
}

static int oxp_hid_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	int ret;
	u16 up;

	ret = hid_parse(hdev);
	if (ret)
		return dev_err_probe(&hdev->dev, ret, "Failed to parse HID device\n");

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return dev_err_probe(&hdev->dev, ret, "Failed to start HID device\n");

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_hw_stop(hdev);
		return dev_err_probe(&hdev->dev, ret, "Failed to open HID device\n");
	}

	up = get_usage_page(hdev);
	dev_dbg(&hdev->dev, "Got usage page %04x\n", up);

	switch (up) {
	case GEN1_USAGE_PAGE:
	case GEN2_USAGE_PAGE:
		ret = oxp_cfg_probe(hdev, up);
		if (ret) {
			hid_hw_close(hdev);
			hid_hw_stop(hdev);
		}

		return ret;
	default:
		return 0;
	}
}

static void oxp_hid_remove(struct hid_device *hdev)
{
	cancel_delayed_work(&drvdata.oxp_rgb_queue);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id oxp_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CRSC, USB_DEVICE_ID_ONEXPLAYER_GEN1) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WCH, USB_DEVICE_ID_ONEXPLAYER_GEN2) },
	{}
};

MODULE_DEVICE_TABLE(hid, oxp_devices);
static struct hid_driver hid_oxp = {
	.name = "hid-oxp",
	.id_table = oxp_devices,
	.probe = oxp_hid_probe,
	.remove = oxp_hid_remove,
	.raw_event = oxp_hid_raw_event,
};
module_hid_driver(hid_oxp);

MODULE_AUTHOR("Derek J. Clark <derekjohn.clark@gmail.com>");
MODULE_DESCRIPTION("Driver for OneXPlayer HID Interfaces");
MODULE_LICENSE("GPL");
