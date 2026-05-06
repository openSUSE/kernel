/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DRBD_GENL_STRUCT_H
#define DRBD_GENL_STRUCT_H

/* hack around predefined gcc/cpp "linux=1",
 * we cannot possibly include <1/drbd_genl.h> */
#undef linux

#include <linux/drbd.h>
#define GENL_MAGIC_VERSION	1
#define GENL_MAGIC_FAMILY	drbd
#define GENL_MAGIC_FAMILY_HDRSZ	sizeof(struct drbd_genlmsghdr)
#define GENL_MAGIC_INCLUDE_FILE <linux/drbd_genl.h>
#include <linux/genl_magic_struct.h>

#endif
