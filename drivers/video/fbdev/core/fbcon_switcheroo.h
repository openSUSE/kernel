/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef FBCON_SWITCHEROO_H
#define FBCON_SWITCHEROO_H

struct fb_info;

void fbcon_switcheroo_client_set_fb(struct fb_info *info);
void fbcon_switcheroo_client_clear_fb(struct fb_info *info);

#endif
