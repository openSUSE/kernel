// SPDX-License-Identifier: GPL-2.0+

#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/vga_switcheroo.h>

#include "fbcon_switcheroo.h"

void fbcon_switcheroo_client_set_fb(struct fb_info *info)
{
	if (info->device && dev_is_pci(info->device))
		vga_switcheroo_client_fb_set(to_pci_dev(info->device), info);
}

void fbcon_switcheroo_client_clear_fb(struct fb_info *info)
{
	if (info->device && dev_is_pci(info->device))
		vga_switcheroo_client_fb_set(to_pci_dev(info->device), NULL);
}
