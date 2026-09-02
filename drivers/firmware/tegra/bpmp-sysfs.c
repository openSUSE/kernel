// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, NVIDIA CORPORATION.
 */

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

#include "bpmp-private.h"

#define TEGRA_BPMP_MBWT_NUM_GROUPS	6
#define TEGRA_BPMP_MBWT_NUM_VCS		3

struct tegra_bpmp_mbwt_attr {
	struct kobj_attribute attr;
	struct tegra_bpmp_mbwt_sysfs *mbwt;
	unsigned int instance;
	unsigned int vc_type;
};

struct tegra_bpmp_mbwt_sysfs {
	struct tegra_bpmp *bpmp;
	struct kobject *root;
	struct kobject *group[TEGRA_BPMP_MBWT_NUM_GROUPS];
	struct kobject *vc[TEGRA_BPMP_MBWT_NUM_GROUPS]
			  [TEGRA_BPMP_MBWT_NUM_VCS];
	struct tegra_bpmp_mbwt_attr attrs[TEGRA_BPMP_MBWT_NUM_GROUPS]
				       [TEGRA_BPMP_MBWT_NUM_VCS];
	/* Serializes bandwidth requests to firmware. */
	struct mutex lock;
};

static const char * const tegra_bpmp_mbwt_group_names[] = {
	"pcie0", "pcie1", "pcie2", "pcie3", "pcie4", "pcie5",
};

static const char * const tegra_bpmp_mbwt_vc_names[] = {
	"pcie_read", "pcie_write", "nvclink",
};

static struct tegra_bpmp_mbwt_attr *
tegra_bpmp_mbwt_attr_from_kobj_attr(struct kobj_attribute *attr)
{
	return container_of(attr, struct tegra_bpmp_mbwt_attr, attr);
}

static ssize_t tegra_bpmp_mbwt_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct tegra_bpmp_mbwt_attr *mbwt_attr;
	struct tegra_bpmp_mbwt_sysfs *mbwt;
	unsigned int bandwidth;
	int err;

	mbwt_attr = tegra_bpmp_mbwt_attr_from_kobj_attr(attr);
	mbwt = mbwt_attr->mbwt;

	mutex_lock(&mbwt->lock);
	err = tegra_bpmp_mbwt_get(mbwt->bpmp, mbwt_attr->instance,
				  mbwt_attr->vc_type, &bandwidth);
	mutex_unlock(&mbwt->lock);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", bandwidth);
}

static ssize_t tegra_bpmp_mbwt_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	struct tegra_bpmp_mbwt_attr *mbwt_attr;
	struct tegra_bpmp_mbwt_sysfs *mbwt;
	unsigned int bandwidth;
	int err;

	err = kstrtou32(buf, 0, &bandwidth);
	if (err)
		return err;

	mbwt_attr = tegra_bpmp_mbwt_attr_from_kobj_attr(attr);
	mbwt = mbwt_attr->mbwt;

	mutex_lock(&mbwt->lock);
	err = tegra_bpmp_mbwt_set(mbwt->bpmp, mbwt_attr->instance,
				  mbwt_attr->vc_type, bandwidth);
	mutex_unlock(&mbwt->lock);
	if (err)
		return err;

	return count;
}

static void tegra_bpmp_mbwt_sysfs_teardown(void *data)
{
	struct tegra_bpmp_mbwt_sysfs *mbwt = data;
	unsigned int i, j;

	for (i = 0; i < TEGRA_BPMP_MBWT_NUM_GROUPS; i++) {
		if (!mbwt->group[i])
			continue;

		for (j = 0; j < TEGRA_BPMP_MBWT_NUM_VCS; j++) {
			if (!mbwt->vc[i][j])
				continue;

			sysfs_remove_file(mbwt->vc[i][j],
					  &mbwt->attrs[i][j].attr.attr);
			kobject_put(mbwt->vc[i][j]);
		}

		kobject_put(mbwt->group[i]);
	}

	kobject_put(mbwt->root);
}

static int tegra_bpmp_mbwt_sysfs_add_group(struct tegra_bpmp_mbwt_sysfs *mbwt,
					   unsigned int instance)
{
	struct tegra_bpmp_mbwt_attr *attr;
	unsigned int vc_type;
	int err;

	mbwt->group[instance] =
		kobject_create_and_add(tegra_bpmp_mbwt_group_names[instance],
				       mbwt->root);
	if (!mbwt->group[instance])
		return -ENOMEM;

	for (vc_type = 0; vc_type < TEGRA_BPMP_MBWT_NUM_VCS; vc_type++) {
		attr = &mbwt->attrs[instance][vc_type];
		mbwt->vc[instance][vc_type] =
			kobject_create_and_add(tegra_bpmp_mbwt_vc_names[vc_type],
					       mbwt->group[instance]);
		if (!mbwt->vc[instance][vc_type])
			return -ENOMEM;

		sysfs_attr_init(&attr->attr.attr);
		attr->attr.attr.name = "bandwidth";
		attr->attr.attr.mode = 0644;
		attr->attr.show = tegra_bpmp_mbwt_show;
		attr->attr.store = tegra_bpmp_mbwt_store;
		attr->mbwt = mbwt;
		attr->instance = instance;
		attr->vc_type = vc_type;

		err = sysfs_create_file(mbwt->vc[instance][vc_type],
					&attr->attr.attr);
		if (err)
			return err;
	}

	return 0;
}

int tegra_bpmp_init_sysfs(struct tegra_bpmp *bpmp)
{
	struct tegra_bpmp_mbwt_sysfs *mbwt;
	unsigned int instance;
	int err;

	if (!tegra_bpmp_mrq_is_supported(bpmp, MRQ_SOCHUB_MBWT))
		return 0;

	if (!tegra_bpmp_mbwt_cmd_is_supported(bpmp, CMD_SOCHUB_MBWT_GET_BW) ||
	    !tegra_bpmp_mbwt_cmd_is_supported(bpmp, CMD_SOCHUB_MBWT_SET_BW))
		return 0;

	mbwt = devm_kzalloc(bpmp->dev, sizeof(*mbwt), GFP_KERNEL);
	if (!mbwt)
		return -ENOMEM;

	mbwt->bpmp = bpmp;
	mutex_init(&mbwt->lock);

	mbwt->root = kobject_create_and_add("mbwt", &bpmp->dev->kobj);
	if (!mbwt->root)
		return -ENOMEM;

	for (instance = 0; instance < TEGRA_BPMP_MBWT_NUM_GROUPS; instance++) {
		err = tegra_bpmp_mbwt_sysfs_add_group(mbwt, instance);
		if (err)
			goto remove_sysfs;
	}

	err = devm_add_action_or_reset(bpmp->dev,
				       tegra_bpmp_mbwt_sysfs_teardown, mbwt);
	if (err)
		return err;

	return 0;

remove_sysfs:
	tegra_bpmp_mbwt_sysfs_teardown(mbwt);

	return err;
}
