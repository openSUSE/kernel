/* EFI secret key generator
 *
 * Copyright (C) 2021 Lee, Chun-Yi <jlee@suse.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/efi.h>
#include <asm/efi.h>

#include "efistub.h"

static struct boot_params *b_params;

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

static void efi_printk_status(char *reason, efi_status_t status)
{
	efi_printk(reason);
	efi_printk((char *)efi_status_to_str(status));
	efi_printk("\n");
}

#define get_efi_var(name, vendor, ...) \
	efi_rt_call(get_variable, (efi_char16_t *)(name), \
		    (efi_guid_t *)(vendor), __VA_ARGS__)

#define set_efi_var(name, vendor, ...) \
	efi_rt_call(set_variable, (efi_char16_t *)(name), \
		    (efi_guid_t *)(vendor), __VA_ARGS__)

static efi_char16_t const secret_key_name[] = {
	'S', 'e', 'c', 'r', 'e', 't', 'K', 'e', 'y', 0
};
#define SECRET_KEY_ATTRIBUTE	(EFI_VARIABLE_NON_VOLATILE | \
				EFI_VARIABLE_BOOTSERVICE_ACCESS)

static efi_status_t get_secret_key(u32 *attributes,
			unsigned long *key_size,
			struct efi_skey_setup_data *skey_setup)
{
	void *key_data;
	efi_status_t status;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
				*key_size, &key_data);
	if (status != EFI_SUCCESS) {
		efi_printk_status("Failed to allocate mem: \n", status);
		return status;
	}
	memset(key_data, 0, *key_size);
	status = get_efi_var(secret_key_name, &EFI_SECRET_GUID,
			     attributes, key_size, key_data);
	if (status != EFI_SUCCESS) {
		efi_printk_status("Failed to get secret key: ", status);
		goto err;
	}

	memset(skey_setup->secret_key, 0, SECRET_KEY_SIZE);
	memcpy(skey_setup->secret_key, key_data,
	       (*key_size >= SECRET_KEY_SIZE) ? SECRET_KEY_SIZE : *key_size);
err:
	efi_bs_call(free_pool, key_data);
	return status;
}

static efi_status_t remove_secret_key(u32 attributes)
{
	efi_status_t status;

	status = set_efi_var(secret_key_name,
			     &EFI_SECRET_GUID, attributes, 0, NULL);
	if (status == EFI_SUCCESS)
		efi_printk("Removed secret key\n");
	else
		efi_printk_status("Failed to remove secret key: ", status);

	return status;
}

static efi_status_t create_secret_key(struct efi_skey_setup_data *skey_setup)
{
	efi_status_t status;

	efi_printk("Create new secret key\n");
	memset(skey_setup->secret_key, 0, SECRET_KEY_SIZE);
	status = efi_get_random_bytes(SECRET_KEY_SIZE,
				      (u8 *)skey_setup->secret_key);
	if (status != EFI_SUCCESS) {
		efi_printk_status("Failed to generate secret key: ", status);
		goto err;
	}

	status = set_efi_var(secret_key_name, &EFI_SECRET_GUID,
			     SECRET_KEY_ATTRIBUTE, SECRET_KEY_SIZE,
			     skey_setup->secret_key);
	if (status != EFI_SUCCESS)
		efi_printk_status("Failed to write secret key: ", status);

err:
	return status;
}

static bool found_regen_flag(void)
{
	u32 attributes = 0;
	unsigned long size = 0;
	void *flag;
	bool regen;
	efi_status_t status;

	/* detect secret key regen flag variable */
	status = get_efi_var(EFI_SECRET_KEY_REGEN, &EFI_SECRET_GUID,
			     &attributes, &size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL)
		return false;

	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
				size, &flag);
	if (status != EFI_SUCCESS)
		return false;

	memset(flag, 0, size);
	status = get_efi_var(EFI_SECRET_KEY_REGEN, &EFI_SECRET_GUID,
			     &attributes, &size, flag);
	if (status == EFI_SUCCESS)
		regen = *(bool *)flag;

	/* clean regen flag */
	set_efi_var(EFI_SECRET_KEY_REGEN, &EFI_SECRET_GUID,
		    attributes, 0, NULL);
err:
	efi_bs_call(free_pool, flag);
	return regen;
}

static efi_status_t regen_secret_key(struct efi_skey_setup_data *skey_setup)
{
	u32 attributes = 0;
	unsigned long key_size = SECRET_KEY_SIZE;
	efi_status_t status;

	status = remove_secret_key(attributes);
	if (status == EFI_SUCCESS)
		status = create_secret_key(skey_setup);
	if (status == EFI_SUCCESS)
		status = get_secret_key(&attributes, &key_size, skey_setup);
}

void efi_setup_secret_key(struct boot_params *params)
{
	struct setup_data *setup_data, *skey_setup_data;
	unsigned long setup_size = 0;
	u32 attributes = 0;
	unsigned long key_size = 0;
	struct efi_skey_setup_data *skey_setup;
	efi_status_t status;

	b_params = params;

	setup_size = sizeof(struct setup_data) + sizeof(struct efi_skey_setup_data);
	status = efi_bs_call(allocate_pool, EFI_LOADER_DATA,
				setup_size, (void **)&skey_setup_data);
	if (status != EFI_SUCCESS) {
		efi_printk("Failed to allocate mem for secret key\n");
		return;
	}
	memset(skey_setup_data, 0, setup_size);
	skey_setup = (struct efi_skey_setup_data *) skey_setup_data->data;

	/* detect the size of secret key variable */
	status = get_efi_var(secret_key_name, &EFI_SECRET_GUID,
			     &attributes, &key_size, NULL);
	skey_setup->detect_status = status;
	switch (status) {
	case EFI_BUFFER_TOO_SMALL:
		status = get_secret_key(&attributes, &key_size, skey_setup);
		if (status != EFI_SUCCESS)
			break;
		if (attributes != SECRET_KEY_ATTRIBUTE) {
			efi_printk("Found a unqualified secret key\n");
			status = regen_secret_key(skey_setup);
		} else if (found_regen_flag()) {
			efi_printk("Regenerate secret key\n");
			status = regen_secret_key(skey_setup);
		}
		break;

	case EFI_NOT_FOUND:
		status = create_secret_key(skey_setup);
		if (status == EFI_SUCCESS) {
			key_size = SECRET_KEY_SIZE;
			status = get_secret_key(&attributes, &key_size, skey_setup);
		}
		break;

	default:
		efi_printk_status("Failed to detect secret key's size: ", status);
	}

	skey_setup->key_size = key_size;
	skey_setup->final_status = status;

	skey_setup_data->type = SETUP_EFI_SECRET_KEY;
	skey_setup_data->len = sizeof(struct efi_skey_setup_data);
	skey_setup_data->next = 0;
	setup_data = (struct setup_data *)params->hdr.setup_data;
	while (setup_data && setup_data->next)
		setup_data = (struct setup_data *)setup_data->next;
	if (setup_data)
		setup_data->next = (unsigned long)skey_setup_data;
	else
		params->hdr.setup_data = (unsigned long)skey_setup_data;
}
