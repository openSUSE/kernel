// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Arm Limited
 */

#include <linux/arm-smccc.h>
#include <linux/array_size.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/nmi.h>
#include <linux/psci.h>
#include <linux/stop_machine.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

#include <uapi/linux/psci.h>

#undef pr_fmt
#define pr_fmt(fmt) "Arm LFA: " fmt

/* LFA v1.0b0 specification */
#define LFA_1_0_FN_BASE			0xc40002e0
#define LFA_1_0_FN(n)			(LFA_1_0_FN_BASE + (n))

#define LFA_1_0_FN_GET_VERSION		LFA_1_0_FN(0)
#define LFA_1_0_FN_CHECK_FEATURE	LFA_1_0_FN(1)
#define LFA_1_0_FN_GET_INFO		LFA_1_0_FN(2)
#define LFA_1_0_FN_GET_INVENTORY	LFA_1_0_FN(3)
#define LFA_1_0_FN_PRIME		LFA_1_0_FN(4)
#define LFA_1_0_FN_ACTIVATE		LFA_1_0_FN(5)
#define LFA_1_0_FN_CANCEL		LFA_1_0_FN(6)

/* CALL_AGAIN flags (returned by SMC) */
#define LFA_PRIME_CALL_AGAIN		BIT(0)
#define LFA_ACTIVATE_CALL_AGAIN		BIT(0)

#define LFA_PRIME_BUDGET_MS		30000		/* 30s cap */
#define LFA_PRIME_DELAY_MS		10		/* 10ms between polls */
#define LFA_ACTIVATE_BUDGET_MS		10000		/* 10s cap */
#define LFA_ACTIVATE_DELAY_MS		10		/* 10ms between polls */

/* LFA return values */
#define LFA_SUCCESS			0
#define LFA_NOT_SUPPORTED		1
#define LFA_BUSY			2
#define LFA_AUTH_ERROR			3
#define LFA_NO_MEMORY			4
#define LFA_CRITICAL_ERROR		5
#define LFA_DEVICE_ERROR		6
#define LFA_WRONG_STATE			7
#define LFA_INVALID_PARAMETERS		8
#define LFA_COMPONENT_WRONG_STATE	9
#define LFA_INVALID_ADDRESS		10
#define LFA_ACTIVATION_FAILED		11

/*
 * Not error codes described by the spec, but used internally when
 * PRIME/ACTIVATE calls return with the CALL_AGAIN bit set.
 */
#define LFA_TIMED_OUT			32
#define LFA_CALL_AGAIN			33

#define LFA_ERROR_STRING(name) \
	[name] = #name

static const char * const lfa_error_strings[] = {
	LFA_ERROR_STRING(LFA_SUCCESS),
	LFA_ERROR_STRING(LFA_NOT_SUPPORTED),
	LFA_ERROR_STRING(LFA_BUSY),
	LFA_ERROR_STRING(LFA_AUTH_ERROR),
	LFA_ERROR_STRING(LFA_NO_MEMORY),
	LFA_ERROR_STRING(LFA_CRITICAL_ERROR),
	LFA_ERROR_STRING(LFA_DEVICE_ERROR),
	LFA_ERROR_STRING(LFA_WRONG_STATE),
	LFA_ERROR_STRING(LFA_INVALID_PARAMETERS),
	LFA_ERROR_STRING(LFA_COMPONENT_WRONG_STATE),
	LFA_ERROR_STRING(LFA_INVALID_ADDRESS),
	LFA_ERROR_STRING(LFA_ACTIVATION_FAILED)
};

enum image_attr_names {
	LFA_ATTR_NAME,
	LFA_ATTR_CURRENT_VERSION,
	LFA_ATTR_PENDING_VERSION,
	LFA_ATTR_ACT_CAPABLE,
	LFA_ATTR_ACT_PENDING,
	LFA_ATTR_MAY_RESET_CPU,
	LFA_ATTR_CPU_RENDEZVOUS,
	LFA_ATTR_FORCE_CPU_RENDEZVOUS,
	LFA_ATTR_ACTIVATE,
	LFA_ATTR_CANCEL,
	LFA_ATTR_NR_IMAGES
};

struct fw_image {
	struct kobject kobj;
	const char *image_name;
	int fw_seq_id;
	u64 current_version;
	u64 pending_version;
	bool activation_capable;
	bool activation_pending;
	bool may_reset_cpu;
	bool cpu_rendezvous;
	bool cpu_rendezvous_forced;
	struct kobj_attribute image_attrs[LFA_ATTR_NR_IMAGES];
};

static struct fw_image *kobj_to_fw_image(struct kobject *kobj)
{
	return container_of(kobj, struct fw_image, kobj);
}

/* A UUID split over two 64-bit registers */
struct uuid_regs {
	u64 uuid_lo;
	u64 uuid_hi;
};

/* A list of known GUIDs, to be shown in the "name" sysfs file. */
static const struct fw_image_uuid {
	const char *name;
	const char *uuid;
} fw_images_uuids[] = {
	{
		.name = "TF-A BL31 runtime",
		.uuid = "47d4086d-4cfe-9846-9b95-2950cbbd5a00",
	},
	{
		.name = "BL33 non-secure payload",
		.uuid = "d6d0eea7-fcea-d54b-9782-9934f234b6e4",
	},
	{
		.name = "TF-RMM",
		.uuid = "6c0762a6-12f2-4b56-92cb-ba8f633606d9",
	},
};

static struct kset *lfa_kset;
static struct workqueue_struct *fw_images_update_wq;
static struct work_struct fw_images_update_work;
static struct attribute *image_default_attrs[LFA_ATTR_NR_IMAGES + 1];

static const struct attribute_group image_attr_group = {
	.attrs = image_default_attrs,
};

static const struct attribute_group *image_default_groups[] = {
	&image_attr_group,
	NULL
};

static int update_fw_images_tree(void);

static const char *lfa_error_string(int error)
{
	if (error > 0)
		return lfa_error_strings[LFA_SUCCESS];

	error = -error;
	if (error < ARRAY_SIZE(lfa_error_strings))
		return lfa_error_strings[error];
	if (error == -LFA_TIMED_OUT)
		return "timed out";

	return lfa_error_strings[LFA_DEVICE_ERROR];
}

static void image_release(struct kobject *kobj)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	kfree(image);
}

static const struct kobj_type image_ktype = {
	.release = image_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = image_default_groups,
};

static void delete_fw_image_node(struct fw_image *image)
{
	kobject_del(&image->kobj);
	kobject_put(&image->kobj);
}

static void remove_invalid_fw_images(struct work_struct *work)
{
	struct kobject *kobj, *tmp;
	struct list_head images_to_delete = LIST_HEAD_INIT(images_to_delete);

	/*
	 * Remove firmware images including directories that are no longer
	 * present in the LFA agent after updating the existing ones.
	 * Delete list images before calling kobject_del() and kobject_put() on
	 * them. Kobject_del() uses kset->list_lock itself which can cause lock
	 * recursion, and kobject_put() may sleep.
	 */
	spin_lock(&lfa_kset->list_lock);
	list_for_each_entry_safe(kobj, tmp, &lfa_kset->list, entry) {
		struct fw_image *image = kobj_to_fw_image(kobj);

		if (image->fw_seq_id == -1)
			list_move_tail(&kobj->entry, &images_to_delete);
	}
	spin_unlock(&lfa_kset->list_lock);

	/*
	 * Now safely remove the sysfs kobjects for the deleted list items
	 */
	list_for_each_entry_safe(kobj, tmp, &images_to_delete, entry) {
		struct fw_image *image = kobj_to_fw_image(kobj);

		delete_fw_image_node(image);
	}
}

static void set_image_flags(struct fw_image *image, int seq_id,
			    u32 image_flags, u64 reg_current_ver,
			    u64 reg_pending_ver)
{
	image->fw_seq_id = seq_id;
	image->current_version = reg_current_ver;
	image->pending_version = reg_pending_ver;
	image->activation_capable = !!(image_flags & BIT(0));
	image->activation_pending = !!(image_flags & BIT(1));
	image->may_reset_cpu = !!(image_flags & BIT(2));
	/* cpu_rendezvous_optional bit has inverse logic in the spec */
	image->cpu_rendezvous = !(image_flags & BIT(3));
}

static unsigned long get_nr_lfa_components(void)
{
	struct arm_smccc_1_2_regs reg = { 0 };

	reg.a0 = LFA_1_0_FN_GET_INFO;
	reg.a1 = 0; /* lfa_info_selector = 0 */

	arm_smccc_1_2_invoke(&reg, &reg);
	if (reg.a0 != LFA_SUCCESS)
		return reg.a0;

	return reg.a1;
}

static int lfa_cancel(void *data)
{
	struct fw_image *image = data;
	struct arm_smccc_1_2_regs reg = { 0 };

	reg.a0 = LFA_1_0_FN_CANCEL;
	reg.a1 = image->fw_seq_id;
	arm_smccc_1_2_invoke(&reg, &reg);

	/*
	 * When firmware activation is called with "skip_cpu_rendezvous=1",
	 * LFA_CANCEL can fail with LFA_BUSY if the activation could not be
	 * cancelled.
	 */
	if (reg.a0 == LFA_SUCCESS) {
		pr_info("Activation cancelled for image %s\n",
			image->image_name);
	} else {
		pr_err("Activation not cancelled for image %s: %s\n",
		       image->image_name, lfa_error_string(reg.a0));
		return -EINVAL;
	}

	return reg.a0;
}

static const char *get_image_name(const struct fw_image *image)
{
	if (image->image_name && image->image_name[0] != '\0')
		return image->image_name;

	return kobject_name(&image->kobj);
}

/*
 * Try a single activation call. The smc_lock writer lock must be held,
 * and it must be called from inside stop_machine() when CPU rendezvous is
 * required.
 */
static int call_lfa_activate(void *data)
{
	struct fw_image *image = data;
	struct arm_smccc_1_2_regs reg = { 0 }, res;

	touch_nmi_watchdog();
	reg.a0 = LFA_1_0_FN_ACTIVATE;
	reg.a1 = image->fw_seq_id;
	/*
	 * As we do not support updates requiring a CPU reset (yet),
	 * we pass 0 in reg.a3 and reg.a4, holding the entry point and
	 * context ID respectively.
	 * cpu_rendezvous_forced is set by the administrator, via sysfs,
	 * cpu_rendezvous is dictated by each firmware component.
	 */
	reg.a2 = !(image->cpu_rendezvous_forced || image->cpu_rendezvous);
	arm_smccc_1_2_invoke(&reg, &res);

	if ((long)res.a0 < 0)
		return (long)res.a0;

	if (res.a1 & LFA_ACTIVATE_CALL_AGAIN)
		return -LFA_CALL_AGAIN;

	return 0;
}

static int activate_fw_image(struct fw_image *image)
{
	ktime_t end = ktime_add_ms(ktime_get(), LFA_ACTIVATE_BUDGET_MS);
	int ret;

retry:
	if (image->cpu_rendezvous_forced || image->cpu_rendezvous)
		ret = stop_machine(call_lfa_activate, image, cpu_online_mask);
	else
		ret = call_lfa_activate(image);

	if (!ret) {
		update_fw_images_tree();

		return 0;
	}

	if (ret == -LFA_CALL_AGAIN) {
		/* SMC returned with call_again flag set */
		if (ktime_before(ktime_get(), end)) {
			msleep_interruptible(LFA_ACTIVATE_DELAY_MS);
			goto retry;
		}

		ret = -LFA_TIMED_OUT;
	}

	lfa_cancel(image);

	pr_err("LFA_ACTIVATE for image %s failed: %s\n",
	       get_image_name(image), lfa_error_string(ret));

	return ret;
}

static int prime_fw_image(struct fw_image *image)
{
	struct arm_smccc_1_2_regs reg = { 0 }, res;
	ktime_t end = ktime_add_ms(ktime_get(), LFA_PRIME_BUDGET_MS);
	int ret;

	if (image->may_reset_cpu) {
		pr_err("CPU reset not supported by kernel driver\n");

		return -EINVAL;
	}

	touch_nmi_watchdog();

	reg.a0 = LFA_1_0_FN_PRIME;
retry:
	/*
	 * LFA_PRIME will return 1 in reg.a1 if the firmware priming
	 * is still in progress. In that case LFA_PRIME will need to
	 * be called again.
	 * reg.a1 will become 0 once the prime process completes.
	 */
	reg.a1 = image->fw_seq_id;
	arm_smccc_1_2_invoke(&reg, &res);
	if ((long)res.a0 < 0) {
		pr_err("LFA_PRIME for image %s failed: %s\n",
		       get_image_name(image),
		       lfa_error_string(res.a0));

		return res.a0;
	}

	if (res.a1 & LFA_PRIME_CALL_AGAIN) {
		/* SMC returned with call_again flag set */
		if (ktime_before(ktime_get(), end)) {
			msleep_interruptible(LFA_PRIME_DELAY_MS);
			goto retry;
		}

		pr_err("LFA_PRIME for image %s timed out",
		       get_image_name(image));

		ret = lfa_cancel(image);
		if (ret != 0)
			return ret;

		return -ETIMEDOUT;
	}

	return 0;
}

static ssize_t name_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	return sysfs_emit(buf, "%s\n", image->image_name);
}

static ssize_t activation_capable_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	return sysfs_emit(buf, "%d\n", image->activation_capable);
}

static void update_fw_image_pending(struct fw_image *image)
{
	struct arm_smccc_1_2_regs reg = { 0 };

	reg.a0 = LFA_1_0_FN_GET_INVENTORY;
	reg.a1 = image->fw_seq_id;
	arm_smccc_1_2_invoke(&reg, &reg);

	if (reg.a0 == LFA_SUCCESS)
		image->activation_pending = !!(reg.a3 & BIT(1));
}

static ssize_t activation_pending_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	/*
	 * Activation pending status can change anytime thus we need to update
	 * and return its current value
	 */
	update_fw_image_pending(image);

	return sysfs_emit(buf, "%d\n", image->activation_pending);
}

static ssize_t may_reset_cpu_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	return sysfs_emit(buf, "%d\n", image->may_reset_cpu);
}

static ssize_t cpu_rendezvous_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	return sysfs_emit(buf, "%d\n", image->cpu_rendezvous);
}

static ssize_t force_cpu_rendezvous_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	struct fw_image *image = kobj_to_fw_image(kobj);
	int ret;

	ret = kstrtobool(buf, &image->cpu_rendezvous_forced);
	if (ret)
		return ret;

	return count;
}

static ssize_t force_cpu_rendezvous_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);

	return sysfs_emit(buf, "%d\n", image->cpu_rendezvous_forced);
}

static ssize_t current_version_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);
	u32 maj, min;

	maj = image->current_version >> 32;
	min = image->current_version & 0xffffffff;

	return sysfs_emit(buf, "%u.%u\n", maj, min);
}

static ssize_t pending_version_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct fw_image *image = kobj_to_fw_image(kobj);
	struct arm_smccc_1_2_regs reg = { 0 };

	/*
	 * Similar to activation pending, this value can change following an
	 * update, we need to retrieve fresh info instead of stale information.
	 */
	reg.a0 = LFA_1_0_FN_GET_INVENTORY;
	reg.a1 = image->fw_seq_id;
	arm_smccc_1_2_invoke(&reg, &reg);
	if (reg.a0 == LFA_SUCCESS) {
		if (reg.a5 != 0 && image->activation_pending) {
			u32 maj, min;

			image->pending_version = reg.a5;
			maj = reg.a5 >> 32;
			min = reg.a5 & 0xffffffff;

			return sysfs_emit(buf, "%u.%u\n", maj, min);
		}
	}

	return sysfs_emit(buf, "N/A\n");
}

static ssize_t activate_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	struct fw_image *image = kobj_to_fw_image(kobj);
	int ret;

	ret = prime_fw_image(image);
	if (ret)
		return -ECANCELED;

	ret = activate_fw_image(image);
	if (ret)
		return -ECANCELED;

	pr_info("%s: successfully activated\n", get_image_name(image));

	return count;
}

static ssize_t cancel_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	struct fw_image *image = kobj_to_fw_image(kobj);
	int ret;

	ret = lfa_cancel(image);
	if (ret != 0)
		return ret;

	return count;
}

static struct kobj_attribute image_attrs_group[LFA_ATTR_NR_IMAGES] = {
	[LFA_ATTR_NAME]			= __ATTR_RO(name),
	[LFA_ATTR_CURRENT_VERSION]	= __ATTR_RO(current_version),
	[LFA_ATTR_PENDING_VERSION]	= __ATTR_RO(pending_version),
	[LFA_ATTR_ACT_CAPABLE]		= __ATTR_RO(activation_capable),
	[LFA_ATTR_ACT_PENDING]		= __ATTR_RO(activation_pending),
	[LFA_ATTR_MAY_RESET_CPU]	= __ATTR_RO(may_reset_cpu),
	[LFA_ATTR_CPU_RENDEZVOUS]	= __ATTR_RO(cpu_rendezvous),
	[LFA_ATTR_FORCE_CPU_RENDEZVOUS]	= __ATTR_RW(force_cpu_rendezvous),
	[LFA_ATTR_ACTIVATE]		= __ATTR_WO(activate),
	[LFA_ATTR_CANCEL]		= __ATTR_WO(cancel)
};

static void init_image_default_attrs(void)
{
	for (int i = 0; i < LFA_ATTR_NR_IMAGES; i++)
		image_default_attrs[i] = &image_attrs_group[i].attr;
	image_default_attrs[LFA_ATTR_NR_IMAGES] = NULL;
}

static void clean_fw_images_tree(void)
{
	struct kobject *kobj, *tmp;
	struct list_head images_to_delete;

	INIT_LIST_HEAD(&images_to_delete);

	spin_lock(&lfa_kset->list_lock);
	list_for_each_entry_safe(kobj, tmp, &lfa_kset->list, entry) {
		list_move_tail(&kobj->entry, &images_to_delete);
	}
	spin_unlock(&lfa_kset->list_lock);

	list_for_each_entry_safe(kobj, tmp, &images_to_delete, entry) {
		struct fw_image *image = kobj_to_fw_image(kobj);

		delete_fw_image_node(image);
	}
}

static int update_fw_image_node(char *fw_uuid, int seq_id,
				u32 image_flags, u64 reg_current_ver,
				u64 reg_pending_ver)
{
	const char *image_name = "";
	struct fw_image *image;
	struct kobject *kobj;
	int i;

	/*
	 * If a fw_image is already in the images list then we just update
	 * its flags and seq_id instead of trying to recreate it.
	 */
	spin_lock(&lfa_kset->list_lock);
	list_for_each_entry(kobj, &lfa_kset->list, entry) {
		if (!strcmp(kobject_name(kobj), fw_uuid)) {
			struct fw_image *image = kobj_to_fw_image(kobj);

			set_image_flags(image, seq_id, image_flags,
					reg_current_ver, reg_pending_ver);
			spin_unlock(&lfa_kset->list_lock);

			return 0;
		}
	}
	spin_unlock(&lfa_kset->list_lock);

	image = kzalloc_obj(*image);
	if (!image)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(fw_images_uuids); i++) {
		if (!strcmp(fw_images_uuids[i].uuid, fw_uuid))
			image_name = fw_images_uuids[i].name;
	}

	image->kobj.kset = lfa_kset;
	image->image_name = image_name;
	image->cpu_rendezvous_forced = true;
	set_image_flags(image, seq_id, image_flags, reg_current_ver,
			reg_pending_ver);
	if (kobject_init_and_add(&image->kobj, &image_ktype, NULL,
				 "%s", fw_uuid)) {
		kobject_put(&image->kobj);

		return -ENOMEM;
	}

	return 0;
}

static int update_fw_images_tree(void)
{
	struct arm_smccc_1_2_regs reg = { 0 }, res;
	struct uuid_regs image_uuid;
	struct kobject *kobj;
	char image_id_str[40];
	int ret, num_of_components;

	num_of_components = get_nr_lfa_components();
	if (num_of_components <= 0) {
		pr_err("Error getting number of LFA components\n");
		return -ENODEV;
	}

	/*
	 * Invalidate fw_seq_ids (-1) for all images as the seq_ids and the
	 * number of firmware images in the LFA agent may change after a
	 * successful activation attempt. Negate all image flags as well.
	 */
	spin_lock(&lfa_kset->list_lock);
	list_for_each_entry(kobj, &lfa_kset->list, entry) {
		struct fw_image *image = kobj_to_fw_image(kobj);

		set_image_flags(image, -1, 0b1000, 0, 0);
	}
	spin_unlock(&lfa_kset->list_lock);

	reg.a0 = LFA_1_0_FN_GET_INVENTORY;
	for (int i = 0; i < num_of_components; i++) {
		reg.a1 = i; /* fw_seq_id to be queried */
		arm_smccc_1_2_invoke(&reg, &res);
		if (res.a0 == LFA_SUCCESS) {
			image_uuid.uuid_lo = res.a1;
			image_uuid.uuid_hi = res.a2;

			snprintf(image_id_str, sizeof(image_id_str), "%pUb",
				 &image_uuid);
			ret = update_fw_image_node(image_id_str, i, res.a3,
						   res.a4, res.a5);
			if (ret)
				return ret;
		}
	}

	/*
	 * Removing non-valid image directories at the end of an activation.
	 * We can't remove the sysfs attributes while in the respective
	 * _store() handler, so have to postpone the list removal to a
	 * workqueue.
	 */
	queue_work(fw_images_update_wq, &fw_images_update_work);

	return 0;
}

static int __init lfa_init(void)
{
	struct arm_smccc_1_2_regs reg = { 0 };
	int err;

	reg.a0 = LFA_1_0_FN_GET_VERSION;
	arm_smccc_1_2_invoke(&reg, &reg);
	if (reg.a0 == -LFA_NOT_SUPPORTED) {
		pr_info("Live Firmware activation: no firmware agent found\n");
		return -ENODEV;
	}

	pr_info("Live Firmware Activation: detected v%ld.%ld\n",
		reg.a0 >> 16, reg.a0 & 0xffff);

	fw_images_update_wq = alloc_workqueue("fw_images_update_wq",
					      WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!fw_images_update_wq) {
		pr_err("Live Firmware Activation: Failed to allocate workqueue.\n");

		return -ENOMEM;
	}
	INIT_WORK(&fw_images_update_work, remove_invalid_fw_images);

	init_image_default_attrs();
	lfa_kset = kset_create_and_add("lfa", NULL, firmware_kobj);
	if (!lfa_kset)
		return -ENOMEM;

	err = update_fw_images_tree();
	if (err != 0) {
		kset_unregister(lfa_kset);
		destroy_workqueue(fw_images_update_wq);
	}

	return err;
}
module_init(lfa_init);

static void __exit lfa_exit(void)
{
	flush_workqueue(fw_images_update_wq);
	destroy_workqueue(fw_images_update_wq);
	clean_fw_images_tree();
	kset_unregister(lfa_kset);
}
module_exit(lfa_exit);

MODULE_DESCRIPTION("ARM Live Firmware Activation (LFA)");
MODULE_LICENSE("GPL");
