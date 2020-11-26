// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 * For more information on Raspberry Pi's PoE hat see:
 * https://www.raspberrypi.org/products/poe-hat/
 *
 * Limitations:
 *  - No disable bit, so a disabled PWM is simulated by duty_cycle 0
 *  - Only normal polarity
 *  - Fixed 12.5 kHz period
 *
 * The current period is completed when HW is reconfigured.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include <soc/bcm2835/raspberrypi-firmware.h>
#include <dt-bindings/pwm/raspberrypi,firmware-poe-pwm.h>

#define RPI_PWM_MAX_DUTY		255
#define RPI_PWM_PERIOD_NS		80000 /* 12.5 kHz */

#define RPI_PWM_CUR_DUTY_REG		0x0
#define RPI_PWM_DEF_DUTY_REG		0x1

struct raspberrypi_pwm {
	struct rpi_firmware *firmware;
	struct pwm_chip chip;
	unsigned int duty_cycle;
};

struct raspberrypi_pwm_prop {
	__le32 reg;
	__le32 val;
	__le32 ret;
} __packed;

static inline struct raspberrypi_pwm *to_raspberrypi_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct raspberrypi_pwm, chip);
}

static int raspberrypi_pwm_set_property(struct rpi_firmware *firmware,
					u32 reg, u32 val)
{
	struct raspberrypi_pwm_prop msg = {
		.reg = cpu_to_le32(reg),
		.val = cpu_to_le32(val),
	};
	int ret;

	ret = rpi_firmware_property(firmware, RPI_FIRMWARE_SET_POE_HAT_VAL,
				    &msg, sizeof(msg));
	if (ret)
		return ret;
	if (msg.ret)
		return -EIO;

	return 0;
}

static int raspberrypi_pwm_get_property(struct rpi_firmware *firmware,
					u32 reg, u32 *val)
{
	struct raspberrypi_pwm_prop msg = {
		.reg = reg
	};
	int ret;

	ret = rpi_firmware_property(firmware, RPI_FIRMWARE_GET_POE_HAT_VAL,
				    &msg, sizeof(msg));
	if (ret)
		return ret;
	if (msg.ret)
		return -EIO;

	*val = le32_to_cpu(msg.val);

	return 0;
}

static void raspberrypi_pwm_get_state(struct pwm_chip *chip,
				      struct pwm_device *pwm,
				      struct pwm_state *state)
{
	struct raspberrypi_pwm *rpipwm = to_raspberrypi_pwm(chip);

	state->period = RPI_PWM_PERIOD_NS;
	state->duty_cycle = DIV_ROUND_CLOSEST(rpipwm->duty_cycle * RPI_PWM_PERIOD_NS,
					      RPI_PWM_MAX_DUTY);
	state->enabled = !!(rpipwm->duty_cycle);
	state->polarity = PWM_POLARITY_NORMAL;
}

static int raspberrypi_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			         struct pwm_state *state)
{
	struct raspberrypi_pwm *rpipwm = to_raspberrypi_pwm(chip);
	unsigned int duty_cycle;
	int ret;

        if (state->period < RPI_PWM_PERIOD_NS ||
            state->polarity != PWM_POLARITY_NORMAL)
                return -EINVAL;

        if (!state->enabled)
                duty_cycle = 0;
        else if (state->duty_cycle < RPI_PWM_PERIOD_NS)
                duty_cycle = DIV_ROUND_CLOSEST_ULL(state->duty_cycle * RPI_PWM_MAX_DUTY,
					           RPI_PWM_PERIOD_NS);
        else
                duty_cycle = RPI_PWM_MAX_DUTY;

	if (duty_cycle == rpipwm->duty_cycle)
		return 0;

	ret = raspberrypi_pwm_set_property(rpipwm->firmware, RPI_PWM_CUR_DUTY_REG,
					   duty_cycle);
	if (ret) {
		dev_err(chip->dev, "Failed to set duty cycle: %d\n", ret);
		return ret;
	}

	/*
	 * This sets the default duty cycle after resetting the board, we
	 * updated it every time to mimic Raspberry Pi's downstream's driver
	 * behaviour.
	 */
	ret = raspberrypi_pwm_set_property(rpipwm->firmware, RPI_PWM_DEF_DUTY_REG,
					   duty_cycle);
	if (ret) {
		dev_err(chip->dev, "Failed to set default duty cycle: %d\n", ret);
		return ret;
	}

        rpipwm->duty_cycle = duty_cycle;

	return 0;
}

static const struct pwm_ops raspberrypi_pwm_ops = {
	.get_state = raspberrypi_pwm_get_state,
	.apply = raspberrypi_pwm_apply,
	.owner = THIS_MODULE,
};

static int raspberrypi_pwm_probe(struct platform_device *pdev)
{
	struct device_node *firmware_node;
	struct device *dev = &pdev->dev;
	struct rpi_firmware *firmware;
	struct raspberrypi_pwm *rpipwm;
	int ret;

	firmware_node = of_get_parent(dev->of_node);
	if (!firmware_node) {
		dev_err(dev, "Missing firmware node\n");
		return -ENOENT;
	}

	firmware = devm_rpi_firmware_get(&pdev->dev, firmware_node);
	of_node_put(firmware_node);
	if (!firmware)
		return -EPROBE_DEFER;

	rpipwm = devm_kzalloc(&pdev->dev, sizeof(*rpipwm), GFP_KERNEL);
	if (!rpipwm)
		return -ENOMEM;

	rpipwm->firmware = firmware;
	rpipwm->chip.dev = dev;
	rpipwm->chip.ops = &raspberrypi_pwm_ops;
	rpipwm->chip.base = -1;
	rpipwm->chip.npwm = RASPBERRYPI_FIRMWARE_PWM_NUM;

	platform_set_drvdata(pdev, rpipwm);

	ret = raspberrypi_pwm_get_property(rpipwm->firmware, RPI_PWM_CUR_DUTY_REG,
					   &rpipwm->duty_cycle);
	if (ret) {
		dev_err(dev, "Failed to get duty cycle: %d\n", ret);
		return ret;
	}

	return pwmchip_add(&rpipwm->chip);
}

static int raspberrypi_pwm_remove(struct platform_device *pdev)
{
	struct raspberrypi_pwm *rpipwm = platform_get_drvdata(pdev);

	return pwmchip_remove(&rpipwm->chip);
}

static const struct of_device_id raspberrypi_pwm_of_match[] = {
	{ .compatible = "raspberrypi,firmware-poe-pwm", },
	{ }
};
MODULE_DEVICE_TABLE(of, raspberrypi_pwm_of_match);

static struct platform_driver raspberrypi_pwm_driver = {
	.driver = {
		.name = "raspberrypi-poe-pwm",
		.of_match_table = raspberrypi_pwm_of_match,
	},
	.probe = raspberrypi_pwm_probe,
	.remove = raspberrypi_pwm_remove,
};
module_platform_driver(raspberrypi_pwm_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de>");
MODULE_DESCRIPTION("Raspberry Pi Firwmare Based PWM Bus Driver");
MODULE_LICENSE("GPL v2");
