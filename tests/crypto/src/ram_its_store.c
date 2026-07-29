/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * A RAM-backed internal trusted storage backend, so PSA can hold the persistent
 * keys ss_crypto.c imports without a flash device. Modelled on the custom store
 * in zephyr/tests/subsys/secure_storage/psa/its.
 *
 * Keys do not need to survive a reboot for these tests -- what matters is that
 * PSA_KEY_LIFETIME_PERSISTENT is accepted and that a key stays retrievable
 * across calls, which is what the real KMU provides on target.
 */

#include <string.h>

#include <zephyr/secure_storage/its/store.h>
#include <zephyr/sys/util.h>

static struct {
	secure_storage_its_uid_t uid;
	size_t data_length;
	uint8_t data[SECURE_STORAGE_ITS_TRANSFORM_MAX_STORED_DATA_SIZE];
} entries[8];

static int find(secure_storage_its_uid_t uid)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(entries); i++) {
		if (memcmp(&uid, &entries[i].uid, sizeof(uid)) == 0) {
			return (int)i;
		}
	}

	return -1;
}

psa_status_t secure_storage_its_store_set(secure_storage_its_uid_t uid, size_t data_length,
					  const void *data)
{
	int index = find(uid);

	if (data_length > sizeof(entries[0].data)) {
		return PSA_ERROR_INSUFFICIENT_STORAGE;
	}

	if (index == -1) {
		for (unsigned int i = 0; i < ARRAY_SIZE(entries); i++) {
			if (entries[i].uid.uid == 0) {
				index = (int)i;
				break;
			}
		}
		if (index == -1) {
			return PSA_ERROR_INSUFFICIENT_STORAGE;
		}
		entries[index].uid = uid;
	}

	entries[index].data_length = data_length;
	memcpy(entries[index].data, data, data_length);

	return PSA_SUCCESS;
}

psa_status_t secure_storage_its_store_get(secure_storage_its_uid_t uid, size_t data_size,
					  void *data, size_t *data_length)
{
	const int index = find(uid);

	if (index == -1) {
		return PSA_ERROR_DOES_NOT_EXIST;
	}

	*data_length = MIN(data_size, entries[index].data_length);
	memcpy(data, entries[index].data, *data_length);

	return PSA_SUCCESS;
}

psa_status_t secure_storage_its_store_remove(secure_storage_its_uid_t uid)
{
	const int index = find(uid);

	if (index == -1) {
		return PSA_ERROR_DOES_NOT_EXIST;
	}

	entries[index].uid.uid = 0;

	return PSA_SUCCESS;
}
