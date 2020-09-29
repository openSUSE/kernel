// SPDX-License-Identifier: GPL-2.0
/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 * Copyright (C) 2003       Pavel Machek (pavel@ucw.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#include <linux/joystick.h>

#include <linux/types.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/compiler.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/ioctl.h>
#include <linux/if.h>
#include <linux/raid/md_u.h>
#include <linux/falloc.h>
#include <linux/file.h>
#include <linux/ppp-ioctl.h>
#include <linux/if_pppox.h>
#include <linux/mtio.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/raw.h>
#include <linux/blkdev.h>
#include <linux/rtc.h>
#include <linux/pci.h>
#include <linux/serial.h>
#include <linux/ctype.h>
#include <linux/syscalls.h>
#include <linux/gfp.h>
#include <linux/cec.h>

#include "internal.h"

#include <linux/capi.h>
#include <linux/gigaset_dev.h>

#ifdef CONFIG_BLOCK
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>
#endif

#include <linux/uaccess.h>
#include <linux/watchdog.h>

#include <linux/hiddev.h>


#include <linux/sort.h>

#ifdef CONFIG_SPARC
#include <linux/fb.h>
#include <asm/fbio.h>
#endif

#define convert_in_user(srcptr, dstptr)			\
({							\
	typeof(*srcptr) val;				\
							\
	get_user(val, srcptr) || put_user(val, dstptr);	\
})

static int do_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err;

	err = security_file_ioctl(file, cmd, arg);
	if (err)
		return err;

	return vfs_ioctl(file, cmd, arg);
}

#ifdef CONFIG_BLOCK
struct compat_sg_req_info { /* used by SG_GET_REQUEST_TABLE ioctl() */
	char req_state;
	char orphan;
	char sg_io_owned;
	char problem;
	int pack_id;
	compat_uptr_t usr_ptr;
	unsigned int duration;
	int unused;
};

static int sg_grt_trans(struct file *file,
		unsigned int cmd, struct compat_sg_req_info __user *o)
{
	int err, i;
	sg_req_info_t __user *r;
	r = compat_alloc_user_space(sizeof(sg_req_info_t)*SG_MAX_QUEUE);
	err = do_ioctl(file, cmd, (unsigned long)r);
	if (err < 0)
		return err;
	for (i = 0; i < SG_MAX_QUEUE; i++) {
		void __user *ptr;
		int d;

		if (copy_in_user(o + i, r + i, offsetof(sg_req_info_t, usr_ptr)) ||
		    get_user(ptr, &r[i].usr_ptr) ||
		    get_user(d, &r[i].duration) ||
		    put_user((u32)(unsigned long)(ptr), &o[i].usr_ptr) ||
		    put_user(d, &o[i].duration))
			return -EFAULT;
	}
	return err;
}
#endif /* CONFIG_BLOCK */

struct sock_fprog32 {
	unsigned short	len;
	compat_caddr_t	filter;
};

#define PPPIOCSPASS32	_IOW('t', 71, struct sock_fprog32)
#define PPPIOCSACTIVE32	_IOW('t', 70, struct sock_fprog32)

static int ppp_sock_fprog_ioctl_trans(struct file *file,
		unsigned int cmd, struct sock_fprog32 __user *u_fprog32)
{
	struct sock_fprog __user *u_fprog64 = compat_alloc_user_space(sizeof(struct sock_fprog));
	void __user *fptr64;
	u32 fptr32;
	u16 flen;

	if (get_user(flen, &u_fprog32->len) ||
	    get_user(fptr32, &u_fprog32->filter))
		return -EFAULT;

	fptr64 = compat_ptr(fptr32);

	if (put_user(flen, &u_fprog64->len) ||
	    put_user(fptr64, &u_fprog64->filter))
		return -EFAULT;

	if (cmd == PPPIOCSPASS32)
		cmd = PPPIOCSPASS;
	else
		cmd = PPPIOCSACTIVE;

	return do_ioctl(file, cmd, (unsigned long) u_fprog64);
}

struct ppp_option_data32 {
	compat_caddr_t	ptr;
	u32			length;
	compat_int_t		transmit;
};
#define PPPIOCSCOMPRESS32	_IOW('t', 77, struct ppp_option_data32)

struct ppp_idle32 {
	compat_time_t xmit_idle;
	compat_time_t recv_idle;
};
#define PPPIOCGIDLE32		_IOR('t', 63, struct ppp_idle32)

static int ppp_gidle(struct file *file, unsigned int cmd,
		struct ppp_idle32 __user *idle32)
{
	struct ppp_idle __user *idle;
	__kernel_time_t xmit, recv;
	int err;

	idle = compat_alloc_user_space(sizeof(*idle));

	err = do_ioctl(file, PPPIOCGIDLE, (unsigned long) idle);

	if (!err) {
		if (get_user(xmit, &idle->xmit_idle) ||
		    get_user(recv, &idle->recv_idle) ||
		    put_user(xmit, &idle32->xmit_idle) ||
		    put_user(recv, &idle32->recv_idle))
			err = -EFAULT;
	}
	return err;
}

static int ppp_scompress(struct file *file, unsigned int cmd,
	struct ppp_option_data32 __user *odata32)
{
	struct ppp_option_data __user *odata;
	__u32 data;
	void __user *datap;

	odata = compat_alloc_user_space(sizeof(*odata));

	if (get_user(data, &odata32->ptr))
		return -EFAULT;

	datap = compat_ptr(data);
	if (put_user(datap, &odata->ptr))
		return -EFAULT;

	if (copy_in_user(&odata->length, &odata32->length,
			 sizeof(__u32) + sizeof(int)))
		return -EFAULT;

	return do_ioctl(file, PPPIOCSCOMPRESS, (unsigned long) odata);
}

#ifdef CONFIG_BLOCK
struct mtget32 {
	compat_long_t	mt_type;
	compat_long_t	mt_resid;
	compat_long_t	mt_dsreg;
	compat_long_t	mt_gstat;
	compat_long_t	mt_erreg;
	compat_daddr_t	mt_fileno;
	compat_daddr_t	mt_blkno;
};
#define MTIOCGET32	_IOR('m', 2, struct mtget32)

struct mtpos32 {
	compat_long_t	mt_blkno;
};
#define MTIOCPOS32	_IOR('m', 3, struct mtpos32)

static int mt_ioctl_trans(struct file *file,
		unsigned int cmd, void __user *argp)
{
	/* NULL initialization to make gcc shut up */
	struct mtget __user *get = NULL;
	struct mtget32 __user *umget32;
	struct mtpos __user *pos = NULL;
	struct mtpos32 __user *upos32;
	unsigned long kcmd;
	void *karg;
	int err = 0;

	switch(cmd) {
	case MTIOCPOS32:
		kcmd = MTIOCPOS;
		pos = compat_alloc_user_space(sizeof(*pos));
		karg = pos;
		break;
	default:	/* MTIOCGET32 */
		kcmd = MTIOCGET;
		get = compat_alloc_user_space(sizeof(*get));
		karg = get;
		break;
	}
	if (karg == NULL)
		return -EFAULT;
	err = do_ioctl(file, kcmd, (unsigned long)karg);
	if (err)
		return err;
	switch (cmd) {
	case MTIOCPOS32:
		upos32 = argp;
		err = convert_in_user(&pos->mt_blkno, &upos32->mt_blkno);
		break;
	case MTIOCGET32:
		umget32 = argp;
		err = convert_in_user(&get->mt_type, &umget32->mt_type);
		err |= convert_in_user(&get->mt_resid, &umget32->mt_resid);
		err |= convert_in_user(&get->mt_dsreg, &umget32->mt_dsreg);
		err |= convert_in_user(&get->mt_gstat, &umget32->mt_gstat);
		err |= convert_in_user(&get->mt_erreg, &umget32->mt_erreg);
		err |= convert_in_user(&get->mt_fileno, &umget32->mt_fileno);
		err |= convert_in_user(&get->mt_blkno, &umget32->mt_blkno);
		break;
	}
	return err ? -EFAULT: 0;
}

#endif /* CONFIG_BLOCK */

/* Bluetooth ioctls */
#define HCIUARTSETPROTO		_IOW('U', 200, int)
#define HCIUARTGETPROTO		_IOR('U', 201, int)
#define HCIUARTGETDEVICE	_IOR('U', 202, int)
#define HCIUARTSETFLAGS		_IOW('U', 203, int)
#define HCIUARTGETFLAGS		_IOR('U', 204, int)

#define RTC_IRQP_READ32		_IOR('p', 0x0b, compat_ulong_t)
#define RTC_IRQP_SET32		_IOW('p', 0x0c, compat_ulong_t)
#define RTC_EPOCH_READ32	_IOR('p', 0x0d, compat_ulong_t)
#define RTC_EPOCH_SET32		_IOW('p', 0x0e, compat_ulong_t)

static int rtc_ioctl(struct file *file,
		unsigned cmd, void __user *argp)
{
	unsigned long __user *valp = compat_alloc_user_space(sizeof(*valp));
	int ret;

	if (valp == NULL)
		return -EFAULT;
	switch (cmd) {
	case RTC_IRQP_READ32:
	case RTC_EPOCH_READ32:
		ret = do_ioctl(file, (cmd == RTC_IRQP_READ32) ?
					RTC_IRQP_READ : RTC_EPOCH_READ,
					(unsigned long)valp);
		if (ret)
			return ret;
		return convert_in_user(valp, (unsigned int __user *)argp);
	case RTC_IRQP_SET32:
		return do_ioctl(file, RTC_IRQP_SET, (unsigned long)argp);
	case RTC_EPOCH_SET32:
		return do_ioctl(file, RTC_EPOCH_SET, (unsigned long)argp);
	}

	return -ENOIOCTLCMD;
}

/*
 * simple reversible transform to make our table more evenly
 * distributed after sorting.
 */
#define XFORM(i) (((i) ^ ((i) << 27) ^ ((i) << 17)) & 0xffffffff)

#define COMPATIBLE_IOCTL(cmd) XFORM((u32)cmd),
/* ioctl should not be warned about even if it's not implemented.
   Valid reasons to use this:
   - It is implemented with ->compat_ioctl on some device, but programs
   call it on others too.
   - The ioctl is not implemented in the native kernel, but programs
   call it commonly anyways.
   Most other reasons are not valid. */
#define IGNORE_IOCTL(cmd) COMPATIBLE_IOCTL(cmd)

static unsigned int ioctl_pointer[] = {
/* compatible ioctls first */
/* Little t */
COMPATIBLE_IOCTL(TIOCOUTQ)
/* Little f */
COMPATIBLE_IOCTL(FIOCLEX)
COMPATIBLE_IOCTL(FIONCLEX)
COMPATIBLE_IOCTL(FIOASYNC)
COMPATIBLE_IOCTL(FIONBIO)
COMPATIBLE_IOCTL(FIONREAD)  /* This is also TIOCINQ */
COMPATIBLE_IOCTL(FS_IOC_FIEMAP)
/* 0x00 */
COMPATIBLE_IOCTL(FIBMAP)
COMPATIBLE_IOCTL(FIGETBSZ)
/* 'X' - originally XFS but some now in the VFS */
COMPATIBLE_IOCTL(FIFREEZE)
COMPATIBLE_IOCTL(FITHAW)
COMPATIBLE_IOCTL(FITRIM)
#ifdef CONFIG_BLOCK
/* Big S */
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_IDLUN)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_DOORUNLOCK)
COMPATIBLE_IOCTL(SCSI_IOCTL_TEST_UNIT_READY)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_BUS_NUMBER)
COMPATIBLE_IOCTL(SCSI_IOCTL_SEND_COMMAND)
COMPATIBLE_IOCTL(SCSI_IOCTL_PROBE_HOST)
COMPATIBLE_IOCTL(SCSI_IOCTL_GET_PCI)
#endif
/* Big V (don't complain on serial console) */
IGNORE_IOCTL(VT_OPENQRY)
IGNORE_IOCTL(VT_GETMODE)
/* Little p (/dev/rtc, /dev/envctrl, etc.) */
COMPATIBLE_IOCTL(RTC_AIE_ON)
COMPATIBLE_IOCTL(RTC_AIE_OFF)
COMPATIBLE_IOCTL(RTC_UIE_ON)
COMPATIBLE_IOCTL(RTC_UIE_OFF)
COMPATIBLE_IOCTL(RTC_PIE_ON)
COMPATIBLE_IOCTL(RTC_PIE_OFF)
COMPATIBLE_IOCTL(RTC_WIE_ON)
COMPATIBLE_IOCTL(RTC_WIE_OFF)
COMPATIBLE_IOCTL(RTC_ALM_SET)
COMPATIBLE_IOCTL(RTC_ALM_READ)
COMPATIBLE_IOCTL(RTC_RD_TIME)
COMPATIBLE_IOCTL(RTC_SET_TIME)
COMPATIBLE_IOCTL(RTC_WKALM_SET)
COMPATIBLE_IOCTL(RTC_WKALM_RD)
/*
 * These two are only for the sbus rtc driver, but
 * hwclock tries them on every rtc device first when
 * running on sparc.  On other architectures the entries
 * are useless but harmless.
 */
COMPATIBLE_IOCTL(_IOR('p', 20, int[7])) /* RTCGET */
COMPATIBLE_IOCTL(_IOW('p', 21, int[7])) /* RTCSET */
/* Little m */
COMPATIBLE_IOCTL(MTIOCTOP)
/* Socket level stuff */
COMPATIBLE_IOCTL(FIOQSIZE)
#ifdef CONFIG_BLOCK
/* md calls this on random blockdevs */
IGNORE_IOCTL(RAID_VERSION)
/* qemu/qemu-img might call these two on plain files for probing */
IGNORE_IOCTL(CDROM_DRIVE_STATUS)
IGNORE_IOCTL(FDGETPRM32)
/* SG stuff */
COMPATIBLE_IOCTL(SG_IO)
COMPATIBLE_IOCTL(SG_SET_TIMEOUT)
COMPATIBLE_IOCTL(SG_GET_TIMEOUT)
COMPATIBLE_IOCTL(SG_EMULATED_HOST)
COMPATIBLE_IOCTL(SG_GET_TRANSFORM)
COMPATIBLE_IOCTL(SG_SET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_RESERVED_SIZE)
COMPATIBLE_IOCTL(SG_GET_SCSI_ID)
COMPATIBLE_IOCTL(SG_SET_FORCE_LOW_DMA)
COMPATIBLE_IOCTL(SG_GET_LOW_DMA)
COMPATIBLE_IOCTL(SG_SET_FORCE_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_PACK_ID)
COMPATIBLE_IOCTL(SG_GET_NUM_WAITING)
COMPATIBLE_IOCTL(SG_SET_DEBUG)
COMPATIBLE_IOCTL(SG_GET_SG_TABLESIZE)
COMPATIBLE_IOCTL(SG_GET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_SET_COMMAND_Q)
COMPATIBLE_IOCTL(SG_GET_VERSION_NUM)
COMPATIBLE_IOCTL(SG_NEXT_CMD_LEN)
COMPATIBLE_IOCTL(SG_SCSI_RESET)
COMPATIBLE_IOCTL(SG_GET_REQUEST_TABLE)
COMPATIBLE_IOCTL(SG_SET_KEEP_ORPHAN)
COMPATIBLE_IOCTL(SG_GET_KEEP_ORPHAN)
#endif
/* PPP stuff */
COMPATIBLE_IOCTL(PPPIOCGFLAGS)
COMPATIBLE_IOCTL(PPPIOCSFLAGS)
COMPATIBLE_IOCTL(PPPIOCGASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGUNIT)
COMPATIBLE_IOCTL(PPPIOCGRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSRASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCGMRU)
COMPATIBLE_IOCTL(PPPIOCSMRU)
COMPATIBLE_IOCTL(PPPIOCSMAXCID)
COMPATIBLE_IOCTL(PPPIOCGXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCSXASYNCMAP)
COMPATIBLE_IOCTL(PPPIOCXFERUNIT)
/* PPPIOCSCOMPRESS is translated */
COMPATIBLE_IOCTL(PPPIOCGNPMODE)
COMPATIBLE_IOCTL(PPPIOCSNPMODE)
COMPATIBLE_IOCTL(PPPIOCGDEBUG)
COMPATIBLE_IOCTL(PPPIOCSDEBUG)
/* PPPIOCSPASS is translated */
/* PPPIOCSACTIVE is translated */
/* PPPIOCGIDLE is translated */
COMPATIBLE_IOCTL(PPPIOCNEWUNIT)
COMPATIBLE_IOCTL(PPPIOCATTACH)
COMPATIBLE_IOCTL(PPPIOCDETACH)
COMPATIBLE_IOCTL(PPPIOCSMRRU)
COMPATIBLE_IOCTL(PPPIOCCONNECT)
COMPATIBLE_IOCTL(PPPIOCDISCONN)
COMPATIBLE_IOCTL(PPPIOCATTCHAN)
COMPATIBLE_IOCTL(PPPIOCGCHAN)
COMPATIBLE_IOCTL(PPPIOCGL2TPSTATS)
/* Raw devices */
COMPATIBLE_IOCTL(RAW_SETBIND)
COMPATIBLE_IOCTL(RAW_GETBIND)
/* Watchdog */
COMPATIBLE_IOCTL(WDIOC_GETSUPPORT)
COMPATIBLE_IOCTL(WDIOC_GETSTATUS)
COMPATIBLE_IOCTL(WDIOC_GETBOOTSTATUS)
COMPATIBLE_IOCTL(WDIOC_GETTEMP)
COMPATIBLE_IOCTL(WDIOC_SETOPTIONS)
COMPATIBLE_IOCTL(WDIOC_KEEPALIVE)
COMPATIBLE_IOCTL(WDIOC_SETTIMEOUT)
COMPATIBLE_IOCTL(WDIOC_GETTIMEOUT)
COMPATIBLE_IOCTL(WDIOC_SETPRETIMEOUT)
COMPATIBLE_IOCTL(WDIOC_GETPRETIMEOUT)
/* Big R */
COMPATIBLE_IOCTL(RNDGETENTCNT)
COMPATIBLE_IOCTL(RNDADDTOENTCNT)
COMPATIBLE_IOCTL(RNDGETPOOL)
COMPATIBLE_IOCTL(RNDADDENTROPY)
COMPATIBLE_IOCTL(RNDZAPENTCNT)
COMPATIBLE_IOCTL(RNDCLEARPOOL)
/* Bluetooth */
COMPATIBLE_IOCTL(HCIUARTSETPROTO)
COMPATIBLE_IOCTL(HCIUARTGETPROTO)
COMPATIBLE_IOCTL(HCIUARTGETDEVICE)
COMPATIBLE_IOCTL(HCIUARTSETFLAGS)
COMPATIBLE_IOCTL(HCIUARTGETFLAGS)
/* CAPI */
COMPATIBLE_IOCTL(CAPI_REGISTER)
COMPATIBLE_IOCTL(CAPI_GET_MANUFACTURER)
COMPATIBLE_IOCTL(CAPI_GET_VERSION)
COMPATIBLE_IOCTL(CAPI_GET_SERIAL)
COMPATIBLE_IOCTL(CAPI_GET_PROFILE)
COMPATIBLE_IOCTL(CAPI_MANUFACTURER_CMD)
COMPATIBLE_IOCTL(CAPI_GET_ERRCODE)
COMPATIBLE_IOCTL(CAPI_INSTALLED)
COMPATIBLE_IOCTL(CAPI_GET_FLAGS)
COMPATIBLE_IOCTL(CAPI_SET_FLAGS)
COMPATIBLE_IOCTL(CAPI_CLR_FLAGS)
COMPATIBLE_IOCTL(CAPI_NCCI_OPENCOUNT)
COMPATIBLE_IOCTL(CAPI_NCCI_GETUNIT)
/* Misc. */
COMPATIBLE_IOCTL(0x41545900)		/* ATYIO_CLKR */
COMPATIBLE_IOCTL(0x41545901)		/* ATYIO_CLKW */
COMPATIBLE_IOCTL(PCIIOC_CONTROLLER)
COMPATIBLE_IOCTL(PCIIOC_MMAP_IS_IO)
COMPATIBLE_IOCTL(PCIIOC_MMAP_IS_MEM)
COMPATIBLE_IOCTL(PCIIOC_WRITE_COMBINE)
/* hiddev */
COMPATIBLE_IOCTL(HIDIOCGVERSION)
COMPATIBLE_IOCTL(HIDIOCAPPLICATION)
COMPATIBLE_IOCTL(HIDIOCGDEVINFO)
COMPATIBLE_IOCTL(HIDIOCGSTRING)
COMPATIBLE_IOCTL(HIDIOCINITREPORT)
COMPATIBLE_IOCTL(HIDIOCGREPORT)
COMPATIBLE_IOCTL(HIDIOCSREPORT)
COMPATIBLE_IOCTL(HIDIOCGREPORTINFO)
COMPATIBLE_IOCTL(HIDIOCGFIELDINFO)
COMPATIBLE_IOCTL(HIDIOCGUSAGE)
COMPATIBLE_IOCTL(HIDIOCSUSAGE)
COMPATIBLE_IOCTL(HIDIOCGUCODE)
COMPATIBLE_IOCTL(HIDIOCGFLAG)
COMPATIBLE_IOCTL(HIDIOCSFLAG)
COMPATIBLE_IOCTL(HIDIOCGCOLLECTIONINDEX)
COMPATIBLE_IOCTL(HIDIOCGCOLLECTIONINFO)
/* joystick */
COMPATIBLE_IOCTL(JSIOCGVERSION)
COMPATIBLE_IOCTL(JSIOCGAXES)
COMPATIBLE_IOCTL(JSIOCGBUTTONS)
COMPATIBLE_IOCTL(JSIOCGNAME(0))

/* fat 'r' ioctls. These are handled by fat with ->compat_ioctl,
   but we don't want warnings on other file systems. So declare
   them as compatible here. */
#define VFAT_IOCTL_READDIR_BOTH32       _IOR('r', 1, struct compat_dirent[2])
#define VFAT_IOCTL_READDIR_SHORT32      _IOR('r', 2, struct compat_dirent[2])

IGNORE_IOCTL(VFAT_IOCTL_READDIR_BOTH32)
IGNORE_IOCTL(VFAT_IOCTL_READDIR_SHORT32)

#ifdef CONFIG_SPARC
/* Sparc framebuffers, handled in sbusfb_compat_ioctl() */
IGNORE_IOCTL(FBIOGTYPE)
IGNORE_IOCTL(FBIOSATTR)
IGNORE_IOCTL(FBIOGATTR)
IGNORE_IOCTL(FBIOSVIDEO)
IGNORE_IOCTL(FBIOGVIDEO)
IGNORE_IOCTL(FBIOSCURPOS)
IGNORE_IOCTL(FBIOGCURPOS)
IGNORE_IOCTL(FBIOGCURMAX)
IGNORE_IOCTL(FBIOPUTCMAP32)
IGNORE_IOCTL(FBIOGETCMAP32)
IGNORE_IOCTL(FBIOSCURSOR32)
IGNORE_IOCTL(FBIOGCURSOR32)
#endif
};

/*
 * Convert common ioctl arguments based on their command number
 *
 * Please do not add any code in here. Instead, implement
 * a compat_ioctl operation in the place that handleѕ the
 * ioctl for the native case.
 */
static long do_ioctl_trans(unsigned int cmd,
		 unsigned long arg, struct file *file)
{
	void __user *argp = compat_ptr(arg);

	switch (cmd) {
	case PPPIOCGIDLE32:
		return ppp_gidle(file, cmd, argp);
	case PPPIOCSCOMPRESS32:
		return ppp_scompress(file, cmd, argp);
	case PPPIOCSPASS32:
	case PPPIOCSACTIVE32:
		return ppp_sock_fprog_ioctl_trans(file, cmd, argp);
#ifdef CONFIG_BLOCK
	case SG_GET_REQUEST_TABLE:
		return sg_grt_trans(file, cmd, argp);
	case MTIOCGET32:
	case MTIOCPOS32:
		return mt_ioctl_trans(file, cmd, argp);
#endif
	/* Not implemented in the native kernel */
	case RTC_IRQP_READ32:
	case RTC_IRQP_SET32:
	case RTC_EPOCH_READ32:
	case RTC_EPOCH_SET32:
		return rtc_ioctl(file, cmd, argp);
	}

	/*
	 * These take an integer instead of a pointer as 'arg',
	 * so we must not do a compat_ptr() translation.
	 */
	switch (cmd) {
	/* RAID */
	case HOT_REMOVE_DISK:
	case HOT_ADD_DISK:
	case SET_DISK_FAULTY:
	case SET_BITMAP_FILE:
		return vfs_ioctl(file, cmd, arg);
	}

	return -ENOIOCTLCMD;
}

static int compat_ioctl_check_table(unsigned int xcmd)
{
	int i;
	const int max = ARRAY_SIZE(ioctl_pointer) - 1;

	BUILD_BUG_ON(max >= (1 << 16));

	/* guess initial offset into table, assuming a
	   normalized distribution */
	i = ((xcmd >> 16) * max) >> 16;

	/* do linear search up first, until greater or equal */
	while (ioctl_pointer[i] < xcmd && i < max)
		i++;

	/* then do linear search down */
	while (ioctl_pointer[i] > xcmd && i > 0)
		i--;

	return ioctl_pointer[i] == xcmd;
}

COMPAT_SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd,
		       compat_ulong_t, arg32)
{
	unsigned long arg = arg32;
	struct fd f = fdget(fd);
	int error = -EBADF;
	if (!f.file)
		goto out;

	/* RED-PEN how should LSM module know it's handling 32bit? */
	error = security_file_ioctl(f.file, cmd, arg);
	if (error)
		goto out_fput;

	/*
	 * To allow the compat_ioctl handlers to be self contained
	 * we need to check the common ioctls here first.
	 * Just handle them with the standard handlers below.
	 */
	switch (cmd) {
	case FIOCLEX:
	case FIONCLEX:
	case FIONBIO:
	case FIOASYNC:
	case FIOQSIZE:
		break;

#if defined(CONFIG_X86_64)
	case FS_IOC_RESVSP_32:
	case FS_IOC_RESVSP64_32:
		error = compat_ioctl_preallocate(f.file, 0, compat_ptr(arg));
		goto out_fput;
	case FS_IOC_UNRESVSP_32:
	case FS_IOC_UNRESVSP64_32:
		error = compat_ioctl_preallocate(f.file, FALLOC_FL_PUNCH_HOLE,
				compat_ptr(arg));
		goto out_fput;
	case FS_IOC_ZERO_RANGE_32:
		error = compat_ioctl_preallocate(f.file, FALLOC_FL_ZERO_RANGE,
				compat_ptr(arg));
		goto out_fput;
#else
	case FS_IOC_RESVSP:
	case FS_IOC_RESVSP64:
		error = ioctl_preallocate(f.file, 0, compat_ptr(arg));
		goto out_fput;
	case FS_IOC_UNRESVSP:
	case FS_IOC_UNRESVSP64:
		error = ioctl_preallocate(f.file, FALLOC_FL_PUNCH_HOLE,
				compat_ptr(arg));
		goto out_fput;
	case FS_IOC_ZERO_RANGE:
		error = ioctl_preallocate(f.file, FALLOC_FL_ZERO_RANGE,
				compat_ptr(arg));
		goto out_fput;
#endif

	case FICLONE:
		goto do_ioctl;
	case FICLONERANGE:
	case FIDEDUPERANGE:
	case FS_IOC_FIEMAP:
	case FIGETBSZ:
		goto found_handler;

	case FIBMAP:
	case FIONREAD:
		if (S_ISREG(file_inode(f.file)->i_mode))
			break;
		/*FALL THROUGH*/

	default:
		if (f.file->f_op->compat_ioctl) {
			error = f.file->f_op->compat_ioctl(f.file, cmd, arg);
			if (error != -ENOIOCTLCMD)
				goto out_fput;
		}

		if (!f.file->f_op->unlocked_ioctl)
			goto do_ioctl;
		break;
	}

	if (compat_ioctl_check_table(XFORM(cmd)))
		goto found_handler;

	error = do_ioctl_trans(cmd, arg, f.file);
	if (error == -ENOIOCTLCMD)
		error = -ENOTTY;

	goto out_fput;

 found_handler:
	arg = (unsigned long)compat_ptr(arg);
 do_ioctl:
	error = do_vfs_ioctl(f.file, fd, cmd, arg);
 out_fput:
	fdput(f);
 out:
	return error;
}

static int __init init_sys32_ioctl_cmp(const void *p, const void *q)
{
	unsigned int a, b;
	a = *(unsigned int *)p;
	b = *(unsigned int *)q;
	if (a > b)
		return 1;
	if (a < b)
		return -1;
	return 0;
}

static int __init init_sys32_ioctl(void)
{
	sort(ioctl_pointer, ARRAY_SIZE(ioctl_pointer), sizeof(*ioctl_pointer),
		init_sys32_ioctl_cmp, NULL);
	return 0;
}
__initcall(init_sys32_ioctl);
