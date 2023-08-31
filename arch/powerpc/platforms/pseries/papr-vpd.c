// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "papr-vpd: " fmt

#include <linux/anon_inodes.h>
#include <linux/build_bug.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/papr-vpd.h>
#include <asm/rtas-work-area.h>
#include <asm/rtas.h>

/*
 * Internal VPD "blob" APIs: for accumulating successive ibm,get-vpd results
 * into a buffer to be attached to a file descriptor.
 */
struct vpd_blob {
	const char *data;
	size_t len;
};

static struct vpd_blob *vpd_blob_new(void)
{
	return kzalloc(sizeof(struct vpd_blob), GFP_KERNEL_ACCOUNT);
}

static void vpd_blob_free(struct vpd_blob *blob)
{
	if (blob) {
		kvfree(blob->data);
		kfree(blob);
	}
}

static int vpd_blob_accumulate(struct vpd_blob *blob, const char *data, size_t len)
{
	const size_t new_len = blob->len + len;
	const size_t old_len = blob->len;
	const char *old_ptr = blob->data;
	char *new_ptr;

	new_ptr = old_ptr ?
		kvrealloc(old_ptr, old_len, new_len, GFP_KERNEL_ACCOUNT) :
		kvmalloc(len, GFP_KERNEL_ACCOUNT);

	if (!new_ptr)
		return -ENOMEM;

	memcpy(&new_ptr[old_len], data, len);
	blob->data = new_ptr;
	blob->len = new_len;
	return 0;
}

/**
 * struct rtas_ibm_get_vpd_params - Parameters (in and out) for ibm,get-vpd.
 *
 * @loc_code: In: Location code buffer. Must be RTAS-addressable.
 * @work_area: In: Work area buffer for results.
 * @sequence: In: Sequence number. Out: Next sequence number.
 * @written: Out: Bytes written by ibm,get-vpd to @work_area.
 * @status: Out: RTAS call status.
 */
struct rtas_ibm_get_vpd_params {
	const struct papr_location_code *loc_code;
	struct rtas_work_area *work_area;
	u32 sequence;
	u32 written;
	s32 status;
};

static int rtas_ibm_get_vpd(struct rtas_ibm_get_vpd_params *params)
{
	const struct papr_location_code *loc_code = params->loc_code;
	struct rtas_work_area *work_area = params->work_area;
	u32 rets[2];
	s32 fwrc;
	int ret;

	do {
		fwrc = rtas_call(rtas_function_token(RTAS_FN_IBM_GET_VPD), 4, 3,
				 rets,
				 __pa(loc_code),
				 rtas_work_area_phys(work_area),
				 rtas_work_area_size(work_area),
				 params->sequence);
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case -1:
		ret = -EIO;
		break;
	case -3:
		ret = -EINVAL;
		break;
	case -4:
		ret = -EAGAIN;
		break;
	case 1:
		params->sequence = rets[0];
		fallthrough;
	case 0:
		params->written = rets[1];
		/*
		 * Kernel or firmware bug, do not continue.
		 */
		if (WARN(params->written > rtas_work_area_size(work_area),
			 "possible write beyond end of work area"))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("unexpected ibm,get-vpd status %d\n", fwrc);
		break;
	}

	params->status = fwrc;
	return ret;
}

struct vpd_sequence_state {
	struct mutex *mutex; /* always &vpd_sequence_mutex */
	struct pin_cookie cookie;
	int error;
	struct rtas_ibm_get_vpd_params params;
};

static void vpd_sequence_begin(struct vpd_sequence_state *state,
			       const struct papr_location_code *loc_code)
{
	static DEFINE_MUTEX(vpd_sequence_mutex);
	/*
	 * Use a static data structure for the location code passed to
	 * RTAS to ensure it's in the RMA and avoid a separate work
	 * area allocation.
	 */
	static struct papr_location_code static_loc_code;

	mutex_lock(&vpd_sequence_mutex);

	static_loc_code = *loc_code;
	*state = (struct vpd_sequence_state) {
		.mutex = &vpd_sequence_mutex,
		.cookie = lockdep_pin_lock(&vpd_sequence_mutex),
		.params = {
			.work_area = rtas_work_area_alloc(SZ_4K),
			.loc_code = &static_loc_code,
			.sequence = 1,
		},
	};
}

static bool vpd_sequence_done(const struct vpd_sequence_state *state)
{
	bool done;

	if (state->error)
		return true;

	switch (state->params.status) {
	case 0:
		if (state->params.written == 0)
			done = false; /* Initial state. */
		else
			done = true; /* All data consumed. */
		break;
	case 1:
		done = false; /* More data available. */
		break;
	default:
		done = true; /* Error encountered. */
		break;
	}

	return done;
}

static bool vpd_sequence_advance(struct vpd_sequence_state *state)
{
	if (vpd_sequence_done(state))
		return false;

	state->error = rtas_ibm_get_vpd(&state->params);

	return state->error == 0;
}

static size_t vpd_sequence_get_buffer(const struct vpd_sequence_state *state, const char **buf)
{
	*buf = rtas_work_area_raw_buf(state->params.work_area);
	return state->params.written;
}

static void vpd_sequence_set_err(struct vpd_sequence_state *state, int err)
{
	state->error = err;
}

static void vpd_sequence_end(struct vpd_sequence_state *state)
{
	rtas_work_area_free(state->params.work_area);
	lockdep_unpin_lock(state->mutex, state->cookie);
	mutex_unlock(state->mutex);
}

static struct vpd_blob *papr_vpd_retrieve(const struct papr_location_code *loc_code)
{
	struct vpd_sequence_state state;
	struct vpd_blob *blob;

	blob = vpd_blob_new();
	if (!blob)
		return ERR_PTR(-ENOMEM);

	vpd_sequence_begin(&state, loc_code);

	while (vpd_sequence_advance(&state)) {
		const char *buf;
		const size_t len = vpd_sequence_get_buffer(&state, &buf);

		vpd_sequence_set_err(&state, vpd_blob_accumulate(blob, buf, len));
	}

	vpd_sequence_end(&state);

	if (!state.error)
		return blob;

	vpd_blob_free(blob);

	return ERR_PTR(state.error);
}

static ssize_t papr_vpd_handle_read(struct file *file, char __user *buf, size_t size, loff_t *off)
{
	struct vpd_blob *blob = file->private_data;

	/* Blobs should always have a valid data pointer and nonzero size. */
	if (WARN_ON_ONCE(!blob->data))
		return -EIO;
	if (WARN_ON_ONCE(blob->len == 0))
		return -EIO;
	return simple_read_from_buffer(buf, size, off, blob->data, blob->len);
}

static int papr_vpd_handle_release(struct inode *inode, struct file *file)
{
	struct vpd_blob *blob = file->private_data;

	vpd_blob_free(blob);

	return 0;
}

static loff_t papr_vpd_handle_seek(struct file *file, loff_t off, int whence)
{
	struct vpd_blob *blob = file->private_data;

	return fixed_size_llseek(file, off, whence, blob->len);
}


static const struct file_operations papr_vpd_handle_ops = {
	.read = papr_vpd_handle_read,
	.llseek = papr_vpd_handle_seek,
	.release = papr_vpd_handle_release,
};

static long papr_vpd_ioctl_create_handle(struct papr_location_code __user *ulc)
{
	struct papr_location_code klc;
	struct vpd_blob *blob;
	struct file *file;
	long err;
	int fd;

	if (copy_from_user(&klc, ulc, sizeof(klc)))
		return -EFAULT;

	if (!string_is_terminated(klc.str, ARRAY_SIZE(klc.str)))
		return -EINVAL;

	blob = papr_vpd_retrieve(&klc);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto free_blob;
	}

	file = anon_inode_getfile("[papr-vpd]", &papr_vpd_handle_ops,
				  blob, O_RDONLY);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto put_fd;
	}

	file->f_mode |= FMODE_LSEEK | FMODE_PREAD;
	fd_install(fd, file);
	return fd;
put_fd:
	put_unused_fd(fd);
free_blob:
	vpd_blob_free(blob);
	return err;
}

/* handler for /dev/papr-vpd */
static long papr_vpd_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (__force void __user *)arg;
	long ret;

	switch (ioctl) {
	case PAPR_VPD_CREATE_HANDLE:
		ret = papr_vpd_ioctl_create_handle(argp);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static const struct file_operations papr_vpd_ops = {
	.unlocked_ioctl = papr_vpd_dev_ioctl,
};

static struct miscdevice papr_vpd_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "papr-vpd",
	.fops = &papr_vpd_ops,
};

static __init int papr_vpd_init(void)
{
	if (!rtas_function_implemented(RTAS_FN_IBM_GET_VPD))
		return -ENODEV;

	return misc_register(&papr_vpd_dev);
}
machine_device_initcall(pseries, papr_vpd_init);
