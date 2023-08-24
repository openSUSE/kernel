// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#ifndef _UAPI_PAPR_VPD_H_
#define _UAPI_PAPR_VPD_H_

#include <linux/types.h>
#include <asm/ioctl.h>

struct papr_location_code {
	/*
	 * PAPR+ 12.3.2.4 Converged Location Code Rules - Length
	 * Restrictions. 79 characters plus nul.
	 */
	char str[80];
};

#define PAPR_VPD_IOCTL_BASE 0xb2

#define PAPR_VPD_IO(nr)         _IO(PAPR_VPD_IOCTL_BASE, nr)
#define PAPR_VPD_IOR(nr, type)  _IOR(PAPR_VPD_IOCTL_BASE, nr, type)
#define PAPR_VPD_IOW(nr, type)  _IOW(PAPR_VPD_IOCTL_BASE, nr, type)
#define PAPR_VPD_IOWR(nr, type) _IOWR(PAPR_VPD_IOCTL_BASE, nr, type)

/*
 * ioctl for /dev/papr-vpd. Returns a VPD handle fd corresponding to
 * the location code.
 */
#define PAPR_VPD_CREATE_HANDLE PAPR_VPD_IOW(0, struct papr_location_code)

#endif /* _UAPI_PAPR_VPD_H_ */
