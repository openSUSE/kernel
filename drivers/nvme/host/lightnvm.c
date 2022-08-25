// SPDX-License-Identifier: GPL-2.0
/*
 * nvme-lightnvm.c - LightNVM NVMe device
 *
 * Copyright (C) 2014-2015 IT University of Copenhagen
 * Initial release: Matias Bjorling <mb@lightnvm.io>
 */

#include "nvme.h"

#include <linux/nvme.h>
#include <linux/lightnvm.h>
#include <uapi/linux/lightnvm.h>

int nvme_nvm_ioctl(struct nvme_ns *ns, unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}

int nvme_nvm_register(struct nvme_ns *ns, char *disk_name, int node)
{
	return 0;
}

void nvme_nvm_unregister(struct nvme_ns *ns)
{
}
