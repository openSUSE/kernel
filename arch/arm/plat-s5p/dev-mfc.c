/* linux/arch/arm/plat-s5p/dev-mfc.c
 *
 * Copyright (C) 2010-2011 Samsung Electronics Co.Ltd
 *
 * Base S5P MFC resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/memblock.h>
#include <linux/ioport.h>

#include <mach/map.h>
#include <plat/devs.h>
#include <plat/irqs.h>
#include <plat/mfc.h>

void __init s5p_mfc_reserve_mem(phys_addr_t rbase, unsigned int rsize,
				phys_addr_t lbase, unsigned int lsize)
{
	if (dma_declare_contiguous(&s5p_device_mfc_r.dev, rsize, rbase, 0))
		printk(KERN_ERR "Failed to reserve memory for MFC device (%u bytes at 0x%08lx)\n",
		       rsize, (unsigned long) rbase);

	if (dma_declare_contiguous(&s5p_device_mfc_l.dev, lsize, lbase, 0))
		printk(KERN_ERR "Failed to reserve memory for MFC device (%u bytes at 0x%08lx)\n",
		       rsize, (unsigned long) rbase);
}
