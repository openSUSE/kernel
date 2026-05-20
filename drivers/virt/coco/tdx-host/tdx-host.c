// SPDX-License-Identifier: GPL-2.0
/*
 * TDX host user interface driver
 *
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/device/faux.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/sysfs.h>

#include <asm/cpu_device_id.h>
#include <asm/seamldr.h>
#include <asm/tdx.h>

static const struct x86_cpu_id tdx_host_ids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_TDX_HOST_PLATFORM, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, tdx_host_ids);

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	const struct tdx_sys_info *tdx_sysinfo = tdx_get_sysinfo();
	const struct tdx_sys_info_version *ver;

	if (!tdx_sysinfo)
		return -ENXIO;

	ver = &tdx_sysinfo->version;

	return sysfs_emit(buf, TDX_VERSION_FMT "\n", ver->major_version,
						     ver->minor_version,
						     ver->update_version);
}
static DEVICE_ATTR_RO(version);

static struct attribute *tdx_host_attrs[] = {
	&dev_attr_version.attr,
	NULL,
};

static const struct attribute_group tdx_host_group = {
	.attrs = tdx_host_attrs,
};

static ssize_t seamldr_version_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct seamldr_info info;
	int ret;

	ret = seamldr_get_info(&info);
	if (ret)
		return ret;

	return sysfs_emit(buf, TDX_VERSION_FMT "\n", info.major_version,
						     info.minor_version,
						     info.update_version);
}

static ssize_t num_remaining_updates_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct seamldr_info info;
	int ret;

	ret = seamldr_get_info(&info);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", info.num_remaining_updates);
}

/*
 * These attributes are intended for managing TDX module updates. Reading
 * them issues a slow, serialized P-SEAMLDR query, so keep them admin-only.
 */
static DEVICE_ATTR_ADMIN_RO(seamldr_version);
static DEVICE_ATTR_ADMIN_RO(num_remaining_updates);

static struct attribute *seamldr_attrs[] = {
	&dev_attr_seamldr_version.attr,
	&dev_attr_num_remaining_updates.attr,
	NULL,
};

static umode_t seamldr_group_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	const struct tdx_sys_info *sysinfo = tdx_get_sysinfo();

	if (!sysinfo)
		return 0;

	if (!tdx_supports_runtime_update(sysinfo))
		return 0;

	/*
	 * This bug makes P-SEAMLDR calls clobber the current VMCS
	 * which breaks KVM. Avoid P-SEAMLDR calls by hiding all
	 * attributes if the CPU has this bug.
	 */
	if (boot_cpu_has_bug(X86_BUG_SEAMRET_INVD_VMCS))
		return 0;

	return attr->mode;
}

static const struct attribute_group seamldr_group = {
	.attrs = seamldr_attrs,
	.is_visible = seamldr_group_visible,
};

static const struct attribute_group *tdx_host_groups[] = {
	&tdx_host_group,
	&seamldr_group,
	NULL,
};

static struct faux_device *fdev;

static int __init tdx_host_init(void)
{
	if (!x86_match_cpu(tdx_host_ids) || !tdx_get_sysinfo())
		return -ENODEV;

	fdev = faux_device_create_with_groups(KBUILD_MODNAME, NULL, NULL, tdx_host_groups);
	if (!fdev)
		return -ENODEV;

	return 0;
}
module_init(tdx_host_init);

static void __exit tdx_host_exit(void)
{
	faux_device_destroy(fdev);
}
module_exit(tdx_host_exit);

MODULE_DESCRIPTION("TDX Host Services");
MODULE_LICENSE("GPL");
