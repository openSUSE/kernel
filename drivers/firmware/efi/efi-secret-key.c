/* EFI secret key
 *
 * Copyright (C) 2017 Lee, Chun-Yi <jlee@suse.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/efi.h>
#include <linux/memblock.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/kobject.h>

static u64 efi_skey_setup;
static void *secret_key;
static bool skey_regen;

#define EFI_STATUS_STR(_status) \
	EFI_##_status : return "EFI_" __stringify(_status)

const char *efi_status_to_str(efi_status_t status)
{
	switch (status) {
	case EFI_STATUS_STR(SUCCESS);
	case EFI_STATUS_STR(LOAD_ERROR);
	case EFI_STATUS_STR(INVALID_PARAMETER);
	case EFI_STATUS_STR(UNSUPPORTED);
	case EFI_STATUS_STR(BAD_BUFFER_SIZE);
	case EFI_STATUS_STR(BUFFER_TOO_SMALL);
	case EFI_STATUS_STR(NOT_READY);
	case EFI_STATUS_STR(DEVICE_ERROR);
	case EFI_STATUS_STR(WRITE_PROTECTED);
	case EFI_STATUS_STR(OUT_OF_RESOURCES);
	case EFI_STATUS_STR(NOT_FOUND);
	case EFI_STATUS_STR(ABORTED);
	case EFI_STATUS_STR(SECURITY_VIOLATION);
	}
	/*
	 * There are two possibilities for this message to be exposed:
	 * - Caller feeds a unknown status code from firmware.
	 * - A new status code be defined in efi.h but we forgot to update
	 *   this function.
	 */
	return "Unknown efi status";
}

void __init parse_efi_secret_key_setup(u64 phys_addr, u32 data_len)
{
	struct setup_data *skey_setup_data;

	/* reserve secret key setup data, will copy and erase later */
	efi_skey_setup = phys_addr + sizeof(struct setup_data);
	memblock_reserve(efi_skey_setup, sizeof(struct efi_skey_setup_data));

	/* clean setup data */
	skey_setup_data = early_memremap(phys_addr, data_len);
	memset(skey_setup_data, 0, sizeof(struct setup_data));
	early_iounmap(skey_setup_data, data_len);
}

static void __init
print_efi_skey_setup_data(struct efi_skey_setup_data *skey_setup)
{
	pr_debug("EFI secret key detection status: %s 0x%lx\n",
		efi_status_to_str(skey_setup->detect_status),
		skey_setup->detect_status);
	pr_debug("EFI secret key getting status: %s 0x%lx\n",
		efi_status_to_str(skey_setup->final_status),
		skey_setup->final_status);
	pr_debug("EFI secret key size: %ld\n", skey_setup->key_size);

	if (skey_setup->final_status == EFI_UNSUPPORTED)
		pr_warn(KERN_CONT "EFI_RNG_PROTOCOL unavailable, hibernation will be lock-down.");
	if (skey_setup->final_status == EFI_SUCCESS &&
	    skey_setup->key_size < SECRET_KEY_SIZE) {
		pr_warn(KERN_CONT "EFI secret key size %ld is less than %d.",
			skey_setup->key_size, SECRET_KEY_SIZE);
		pr_warn(KERN_CONT " Please regenerate secret key\n");
	}
}

static int __init init_efi_secret_key(void)
{
	struct efi_skey_setup_data *skey_setup;
	int ret = 0;

	if (!efi_skey_setup)
		return -ENODEV;

	skey_setup = early_memremap(efi_skey_setup,
				    sizeof(struct efi_skey_setup_data));
	print_efi_skey_setup_data(skey_setup);
	if ((skey_setup->final_status != EFI_SUCCESS) ||
	    (skey_setup->key_size < SECRET_KEY_SIZE)) {
		ret = -ENODEV;
		goto out;
	}
	secret_key = memcpy_to_hidden_area(skey_setup->secret_key,
					   SECRET_KEY_SIZE);
	if (!secret_key)
		pr_info("copy secret key to hidden area failed\n");

out:
	/* earse key in setup data */
	memset(skey_setup->secret_key, 0, SECRET_KEY_SIZE);
	early_iounmap(skey_setup, sizeof(struct efi_skey_setup_data));

	return ret;
}

void *get_efi_secret_key(void)
{
	return secret_key;
}
EXPORT_SYMBOL(get_efi_secret_key);

late_initcall(init_efi_secret_key);

static int set_regen_flag(void)
{
	efi_status_t status = EFI_UNSUPPORTED;
	bool regen = true;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	if (efi_rt_services_supported(EFI_RT_SUPPORTED_SET_VARIABLE))
		status = efi.set_variable(EFI_SECRET_KEY_REGEN, &EFI_SECRET_GUID,
					  EFI_SECRET_KEY_REGEN_ATTRIBUTE,
					  sizeof(bool), &regen);
	if (status != EFI_SUCCESS)
		pr_warn("Create EFI secret key regen failed: 0x%lx\n", status);

	return efi_status_to_err(status);
}

static int clean_regen_flag(void)
{
	efi_status_t status = EFI_UNSUPPORTED;

	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	if (efi_rt_services_supported(EFI_RT_SUPPORTED_SET_VARIABLE))
		status = efi.set_variable(EFI_SECRET_KEY_REGEN, &EFI_SECRET_GUID,
					  EFI_SECRET_KEY_REGEN_ATTRIBUTE,
				          0, NULL);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		pr_warn("Clean EFI secret key regen failed: 0x%lx\n", status);

	return efi_status_to_err(status);
}

void efi_skey_stop_regen(void)
{
	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return;

	if (!clean_regen_flag())
		skey_regen = false;
}
EXPORT_SYMBOL(efi_skey_stop_regen);

static struct kobject *secret_key_kobj;

static ssize_t regen_show(struct kobject *kobj,
			  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", skey_regen);
}

static ssize_t regen_store(struct kobject *kobj,
			   struct kobj_attribute *attr,
			   const char *buf, size_t size)
{
	bool regen_in;
	int ret;

	ret = strtobool(buf, &regen_in);
	if (ret < 0)
		return ret;

	if (!skey_regen && regen_in) {
		ret = set_regen_flag();
		if (ret < 0)
			return ret;
	}

	if (skey_regen && !regen_in) {
		ret = clean_regen_flag();
		if (ret < 0)
			return ret;
	}

	skey_regen = regen_in;

	return size;
}

static const struct kobj_attribute regen_attr =
	__ATTR(regen, 0644, regen_show, regen_store);

int __init efi_skey_sysfs_init(struct kobject *efi_kobj)
{
	secret_key_kobj = kobject_create_and_add("secret-key", efi_kobj);
	if (!secret_key_kobj)
		return -ENOMEM;

	return sysfs_create_file(secret_key_kobj, &regen_attr.attr);
}
