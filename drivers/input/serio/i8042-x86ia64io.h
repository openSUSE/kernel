#ifndef _I8042_X86IA64IO_H
#define _I8042_X86IA64IO_H

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * Names.
 */

#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"
#define I8042_MUX_PHYS_DESC "isa0060/serio%d"

/*
 * IRQs.
 */

#if defined(__ia64__)
# define I8042_MAP_IRQ(x)	isa_irq_to_vector((x))
#else
# define I8042_MAP_IRQ(x)	(x)
#endif

#define I8042_KBD_IRQ	i8042_kbd_irq
#define I8042_AUX_IRQ	i8042_aux_irq

static int i8042_kbd_irq;
static int i8042_aux_irq;

/*
 * Register numbers.
 */

#define I8042_COMMAND_REG	i8042_command_reg
#define I8042_STATUS_REG	i8042_command_reg
#define I8042_DATA_REG		i8042_data_reg

static int i8042_command_reg = 0x64;
static int i8042_data_reg = 0x60;


static inline int i8042_read_data(void)
{
	return inb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return inb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	outb(val, I8042_DATA_REG);
}

static inline void i8042_write_command(int val)
{
	outb(val, I8042_COMMAND_REG);
}

#if defined(__i386__)

#include <linux/dmi.h>

static struct dmi_system_id __initdata i8042_dmi_table[] = {
	{
		.ident = "Compaq Proliant 8500",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME , "ProLiant"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "8500"),
		},
	},
	{
		.ident = "Compaq Proliant DL760",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
			DMI_MATCH(DMI_PRODUCT_NAME , "ProLiant"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "DL760"),
		},
	},
	{ }
};
#endif


#ifdef CONFIG_PNP
#include <linux/pnp.h>
#include <linux/acpi.h>

static int i8042_pnp_kbd_registered;
static int i8042_pnp_aux_registered;


static int i8042_pnp_kbd_probe(struct pnp_dev *dev, const struct pnp_device_id *did)
{
	if (pnp_port_valid(dev, 0) && pnp_port_len(dev, 0) == 1) {
		if ((pnp_port_start(dev,0) & ~0xf) == 0x60 && pnp_port_start(dev,0) != 0x60)
			printk(KERN_WARNING "PNP: [%s] has invalid data port %#lx; default is %#x\n",
	                        pnp_dev_name(dev), pnp_port_start(dev,0), i8042_data_reg);
		else
			i8042_data_reg = pnp_port_start(dev,0);
	} else
		printk(KERN_WARNING "PNP: [%s] has no data port; default is 0x%x\n",
			pnp_dev_name(dev), i8042_data_reg);

	if (pnp_port_valid(dev, 1) && pnp_port_len(dev, 1) == 1) {
		if ((pnp_port_start(dev,1) & ~0xf) == 0x60 && pnp_port_start(dev,1) != 0x64)
			printk(KERN_WARNING "PNP: [%s] has invalid command port %#lx; default is %#x\n",
	                        pnp_dev_name(dev), pnp_port_start(dev,1), i8042_command_reg);
		else
			i8042_command_reg = pnp_port_start(dev,1);
	} else
		printk(KERN_WARNING "PNP: [%s] has no command port; default is 0x%x\n",
			pnp_dev_name(dev), i8042_command_reg);

	if (pnp_irq_valid(dev,0))
		i8042_kbd_irq = pnp_irq(dev,0);
	else
		printk(KERN_WARNING "PNP: [%s] has no IRQ; default is %d\n",
			pnp_dev_name(dev), i8042_kbd_irq);

	printk(KERN_INFO "PNP: %s [%s,%s] at %#x,%#x irq %d\n",
		"PS/2 Keyboard Controller", did->id, pnp_dev_name(dev),
		i8042_data_reg, i8042_command_reg, i8042_kbd_irq);

	return 0;
}

static int i8042_pnp_aux_probe(struct pnp_dev *dev, const struct pnp_device_id *did)
{
	if (pnp_port_valid(dev, 0) && pnp_port_len(dev, 0) == 1) {
		if ((pnp_port_start(dev,0) & ~0xf) == 0x60 && pnp_port_start(dev,0) != 0x60)
			printk(KERN_WARNING "PNP: [%s] has invalid data port %#lx; default is %#x\n",
	                        pnp_dev_name(dev), pnp_port_start(dev,0), i8042_data_reg);
		else
			i8042_data_reg = pnp_port_start(dev,0);
	} else
		printk(KERN_WARNING "PNP: [%s] has no data port; default is 0x%x\n",
			pnp_dev_name(dev), i8042_data_reg);

	if (pnp_port_valid(dev, 1) && pnp_port_len(dev, 1) == 1) {
		if ((pnp_port_start(dev,1) & ~0xf) == 0x60 && pnp_port_start(dev,1) != 0x64)
			printk(KERN_WARNING "PNP: [%s] has invalid command port %#lx; default is %#x\n",
	                        pnp_dev_name(dev), pnp_port_start(dev,1), i8042_command_reg);
		else
			i8042_command_reg = pnp_port_start(dev,1);
	} else
		printk(KERN_WARNING "PNP: [%s] has no command port; default is 0x%x\n",
			pnp_dev_name(dev), i8042_command_reg);

	if (pnp_irq_valid(dev,0))
		i8042_aux_irq = pnp_irq(dev,0);
	else
		printk(KERN_WARNING "PNP: [%s] has no IRQ; default is %d\n",
			pnp_dev_name(dev), i8042_aux_irq);

	printk(KERN_INFO "PNP: %s [%s,%s] at %#x,%#x irq %d\n",
		"PS/2 Mouse Controller", did->id, pnp_dev_name(dev),
		i8042_data_reg, i8042_command_reg, i8042_aux_irq);

	return 0;
}

static struct pnp_device_id pnp_kbd_devids[] = {
	{ .id = "PNP0303", .driver_data = 0 },
	{ .id = "PNP030b", .driver_data = 0 },
	{ .id = "", },
};

static struct pnp_driver i8042_pnp_kbd_driver = {
	.name           = "i8042 kbd",
	.id_table       = pnp_kbd_devids,
	.probe          = i8042_pnp_kbd_probe,
};

static struct pnp_device_id pnp_aux_devids[] = {
	{ .id = "PNP0f13", .driver_data = 0 },
	{ .id = "SYN0801", .driver_data = 0 },
	{ .id = "", },
};

static struct pnp_driver i8042_pnp_aux_driver = {
	.name           = "i8042 aux",
	.id_table       = pnp_aux_devids,
	.probe          = i8042_pnp_aux_probe,
};

static void i8042_pnp_exit(void)
{
	if (i8042_pnp_kbd_registered)
		pnp_unregister_driver(&i8042_pnp_kbd_driver);

	if (i8042_pnp_aux_registered)
		pnp_unregister_driver(&i8042_pnp_aux_driver);
}

static int i8042_pnp_init(void)
{
	int result_kbd, result_aux;

	if (i8042_nopnp) {
		printk("i8042: PNP detection disabled\n");
		return 0;
	}

	if ((result_kbd = pnp_register_driver(&i8042_pnp_kbd_driver)) >= 0)
		i8042_pnp_kbd_registered = 1;
	if ((result_aux = pnp_register_driver(&i8042_pnp_aux_driver)) >= 0)
		i8042_pnp_aux_registered = 1;

/*
 * Only fail if we're rather sure there is
 * no AUX/KBD controller.
 */

#ifdef CONFIG_PNPACPI
	if (!acpi_disabled) {
		if (result_aux <= 0)
			i8042_noaux = 1;
		if (result_kbd <= 0 && result_aux <= 0) {
			i8042_pnp_exit();
			return -ENODEV;
		}
	}
#endif
	
	return 0;
}

#endif

static inline int i8042_platform_init(void)
{
/*
 * On ix86 platforms touching the i8042 data register region can do really
 * bad things. Because of this the region is always reserved on ix86 boxes.
 *
 *	if (!request_region(I8042_DATA_REG, 16, "i8042"))
 *		return -1;
 */

	i8042_kbd_irq = I8042_MAP_IRQ(1);
	i8042_aux_irq = I8042_MAP_IRQ(12);

#ifdef CONFIG_PNP
	if (i8042_pnp_init())
		return -1;
#endif

#if defined(__ia64__)
        i8042_reset = 1;
#endif

#if defined(__i386__)
	if (dmi_check_system(i8042_dmi_table))
		i8042_noloop = 1;
#endif

	return 0;
}

static inline void i8042_platform_exit(void)
{
#ifdef CONFIG_PNP
	i8042_pnp_exit();
#endif
}

#endif /* _I8042_X86IA64IO_H */
