// SPDX-License-Identifier: GPL-2.0+
/*
 * PM MFD driver for Broadcom BCM2835
 *
 * This driver binds to the PM block and creates the MFD device for
 * the WDT and power drivers.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/bcm2835-pm.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define BCM2711		BIT(1)

static const struct mfd_cell bcm2835_pm_devs[] = {
	{ .name = "bcm2835-wdt" },
};

static const struct mfd_cell bcm2835_power_devs[] = {
	{ .name = "bcm2835-power" },
};

static int bcm2835_pm_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct bcm2835_pm *pm;
	int ret;

	pm = devm_kzalloc(dev, sizeof(*pm), GFP_KERNEL);
	if (!pm)
		return -ENOMEM;
	platform_set_drvdata(pdev, pm);

	pm->dev = dev;
	pm->is_bcm2711 = (uintptr_t)device_get_match_data(&pdev->dev) & BCM2711;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pm->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pm->base))
		return PTR_ERR(pm->base);

	ret = devm_mfd_add_devices(dev, -1,
				   bcm2835_pm_devs, ARRAY_SIZE(bcm2835_pm_devs),
				   NULL, 0, NULL);
	if (ret)
		return ret;

	/* To support old firmware, check if a third resource was defined and
	 * use that as a hint that we're on bcm2711.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (res) {
		pm->asb = devm_ioremap_resource(dev, res);
		if (IS_ERR(pm->asb)) {
			dev_err(dev, "Failed to map RPiVid ASB: %ld\n",
				PTR_ERR(pm->asb));
			return PTR_ERR(pm->asb);
		}
		pm->is_bcm2711 = true;
	}

	/* We'll use the presence of the AXI ASB regs in the
	 * bcm2835-pm binding as the key for whether we can reference
	 * the full PM register range and support power domains. Bypass this if
	 * a resource was found at index 2.
	 */
	if (!pm->asb) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (res) {
			pm->asb = devm_ioremap_resource(dev, res);
			if (IS_ERR(pm->asb))
				return PTR_ERR(pm->asb);
		}
	}

	return devm_mfd_add_devices(dev, -1, bcm2835_power_devs,
				    ARRAY_SIZE(bcm2835_power_devs),
				    NULL, 0, NULL);
}

static const struct of_device_id bcm2835_pm_of_match[] = {
	{ .compatible = "brcm,bcm2835-pm-wdt", },
	{ .compatible = "brcm,bcm2835-pm", },
	{ .compatible = "brcm,bcm2711-pm", .data = (void *)BCM2711},
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_pm_of_match);

static struct platform_driver bcm2835_pm_driver = {
	.probe		= bcm2835_pm_probe,
	.driver = {
		.name =	"bcm2835-pm",
		.of_match_table = bcm2835_pm_of_match,
	},
};
module_platform_driver(bcm2835_pm_driver);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("Driver for Broadcom BCM2835 PM MFD");
MODULE_LICENSE("GPL");
