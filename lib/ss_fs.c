/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdlib.h>

#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include "ss_cache.h"
#include <onomondo/softsim/fs.h>
#include <onomondo/softsim/storage.h>
#include <onomondo/softsim/utils.h>
#include <onomondo/softsim/log.h>
#include <onomondo/softsim/mem.h>
#include <onomondo/utils/ss_profile.h>

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

/* PARTITION_* resolves from the Partition Manager shim (flash_map_pm.h) when
 * PM is enabled, and from the devicetree nvs_storage node label otherwise. */
#define NVS_PARTITION        nvs_storage
#define NVS_PARTITION_DEVICE PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   PARTITION_SIZE(NVS_PARTITION)

#define DIR_ID (1UL)

#define IMSI_PATH  "/3f00/7ff0/6f07"
#define ICCID_PATH "/3f00/2fe2"
#define A001_PATH  "/3f00/a001"
#define A004_PATH  "/3f00/a004"
#define SMSP_PATH  "/3f00/7ff0/6f42"

/* Byte offset of the TP-Service-Centre-Address (SMSC) inside an EF.SMSP record.
 * Per 3GPP the record is [alpha-id(24) | param-indicators(1) | TP-DA(12) |
 * TP-SCA(12) | ...], so the SMSC sits at 24 + 1 + 12 = 37. */
#define SMSC_REC_OFFSET 37

/* The onomondo-uicc profile parser keeps EF contents as hex-ASCII (the *_LEN
 * macros in <onomondo/utils/ss_profile.h> are char counts). The nrf port stores
 * the compact binary form (CONFIG_COMPACT_STORAGE), so derive the on-flash byte
 * widths here. */
#define ICCID_BIN_LEN (ICCID_LEN / 2)
#define IMSI_BIN_LEN  (IMSI_LEN / 2)
#define A001_BIN_LEN  (A001_LEN / 2)
#define A004_BIN_LEN  (A004_LEN / 2)
#define KEY_BIN_LEN   (KEY_SIZE / 2)

#ifndef SEEK_SET
#define SEEK_SET 0 /* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1 /* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define SEEK_END 2 /* set file offset to EOF plus offset */
#endif

static struct nvs_fs fs;

/* One 8-byte entry per file, in one allocation; the paths themselves live
 * nowhere (lookups go through the hash). Content buffers are attached to the
 * fixed slot table, never to the directory. */
static struct ss_dir_entry *fs_dir;
static size_t fs_dir_count;
static struct ss_cache_slot fs_slots[SS_MAX_ENTRIES];

static uint8_t fs_is_initialized = 0;
static uint8_t default_imsi[] = {0x08, 0x09, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x10};

/* Path prefix the onomondo-uicc compact-storage backend (storage_compact.c)
 * prepends to every host path. The nrf port uses bare file IDs as NVS keys, so
 * the prefix is empty. Defined here because the file that normally provides it
 * (onomondo-uicc/src/softsim/fs.c) is not compiled under CONFIG_COMPACT_STORAGE;
 * it is declared extern in <onomondo/softsim/storage.h>. */
char storage_path[SS_STORAGE_PATH_MAX] = "";

/**
 * @brief Internal function to read NVS data into a cache slot
 *
 * @param dir_idx Directory index of the file to load
 * @param len Length of the file in NVS
 *
 * Acquires a slot (evicting by the ss_slot_acquire() preference once the
 * cache is full), writes a dirty victim back to NVS, reuses the victim's
 * buffer when it is large enough, and reads the file content from NVS.
 *
 * @return The slot holding the content, or NULL on allocation/NVS failure
 */
static struct ss_cache_slot *ss_load_to_slot(uint16_t dir_idx, uint16_t len);

/* See <onomondo/softsim/fs.h> in the onomondo-uicc submodule */
int ss_init_fs(void)
{
	if (fs_is_initialized) {
		return 0; /* Already initialized */
	}

	uint8_t *data = NULL;
	size_t len = 0;

	fs.flash_device = NVS_PARTITION_DEVICE;
	fs.sector_size =
		0x1000; /* Where to read this? :DT_PROP(NVS_PARTITION, erase_block_size); */
	fs.sector_count = NVS_PARTITION_SIZE / fs.sector_size;
	fs.offset = NVS_PARTITION_OFFSET;

	int rc = nvs_mount(&fs);
	if (rc) {
		LOG_ERR("Failed to mount NVS");
		return -1;
	}

	rc = nvs_read(&fs, DIR_ID, NULL, 0);
	if (rc < 0) {
		/* No DIR entry yet (e.g. -ENOENT before provisioning) or an NVS
		 * error. Don't assign a negative rc to the size_t len. Treat it
		 * like an empty DIR blob (the existing rc == 0 path). */
		LOG_WRN("No DIR entry in NVS (%d); starting with empty filesystem cache", rc);
		rc = 0;
	}
	len = rc;

	/* Read DIR_ENTRY from NVS
	 * This is used to construct the directory table that serves as the
	 * lookup table for the filesystem
	 */
	if (!data && rc) {
		data = SS_ALLOC_N(len * sizeof(uint8_t));
		rc = nvs_read(&fs, DIR_ID, data, len);
		__ASSERT_NO_MSG(rc == len);
	}

	memset(fs_slots, 0, sizeof(fs_slots));
	rc = ss_dir_table_from_blob(data, data ? len : 0, &fs_dir);
	fs_dir_count = rc > 0 ? (size_t)rc : 0;

	if (fs_dir_count > 0) {
		fs_is_initialized++;
	}

	SS_FREE(data);
	return fs_dir_count == 0;
}

/* See <onomondo/softsim/fs.h> in the onomondo-uicc submodule */
int ss_deinit_fs(void)
{
	/* Commit changes to NVS and free the cache buffers and the table */
	for (size_t i = 0; i < SS_MAX_ENTRIES; i++) {
		struct ss_cache_slot *slot = &fs_slots[i];

		if (!slot->buf) {
			continue;
		}

		if (slot->_b_dirty) {
			LOG_INF("SoftSIM stop - committing key 0x%04x to NVS",
				fs_dir[slot->dir_idx].key);
			nvs_write(&fs, fs_dir[slot->dir_idx].key, slot->buf, slot->_l);
		}

		SS_FREE(slot->buf);
	}

	memset(fs_slots, 0, sizeof(fs_slots));
	SS_FREE(fs_dir);
	fs_dir = NULL;
	fs_dir_count = 0;
	fs_is_initialized = 0;

	return 0;
}

/* See <onomondo/softsim/fs.h> in the onomondo-uicc submodule */
ss_FILE ss_fopen(char *path, char *mode)
{
	int dir_idx = ss_dir_find(fs_dir, fs_dir_count, path);

	if (dir_idx < 0) {
		return NULL;
	}

	/* Currently only used to bias eviction towards rarely-opened files. */
	if (fs_dir[dir_idx].hits < 0xFF) {
		fs_dir[dir_idx].hits++;
	}

	int slot_idx = ss_slot_find(fs_slots, SS_MAX_ENTRIES, (uint16_t)dir_idx);

	if (slot_idx >= 0) {
		/* Reset internal read/write pointer */
		fs_slots[slot_idx]._p = 0;
		return &fs_slots[slot_idx];
	}

	int rc = nvs_read(&fs, fs_dir[dir_idx].key, NULL, 0);

	if (rc < 0) {
		return NULL;
	}

	/* NULL on allocation or NVS failure: fail closed rather than hand out
	 * a handle without content behind it. */
	return ss_load_to_slot((uint16_t)dir_idx, (uint16_t)rc);
}

/* Strong override of the weak ss_file_size declared in
 * <onomondo/softsim/fs.h>. The POSIX default in
 * onomondo-uicc/src/softsim/fs.c is not compiled when
 * CONFIG_COMPACT_STORAGE=y, so this file provides the symbol via the
 * nrf-softsim cache layer instead. */
int ss_file_size(const char *path)
{
	int dir_idx = ss_dir_find(fs_dir, fs_dir_count, path);

	if (dir_idx < 0) {
		return -1;
	}

	int slot_idx = ss_slot_find(fs_slots, SS_MAX_ENTRIES, (uint16_t)dir_idx);

	if (slot_idx >= 0) {
		return (int)fs_slots[slot_idx]._l;
	}

	int rc = nvs_read(&fs, fs_dir[dir_idx].key, NULL, 0);

	return rc < 0 ? -1 : rc;
}

/* See <onomondo/softsim/fs.h> in the onomondo-uicc submodule */
size_t ss_fread(void *ptr, size_t size, size_t nmemb, ss_FILE fp)
{
	if (nmemb == 0 || size == 0) {
		return 0;
	}

	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;
	size_t max_element_to_return = (slot->_l - slot->_p) / size;
	size_t element_to_return = nmemb > max_element_to_return ? max_element_to_return : nmemb;

	/* Copy data from cache to user buffer */
	memcpy(ptr, slot->buf + slot->_p, element_to_return * size);
	/* Update internal read/write pointer */
	slot->_p += element_to_return * size;

	return element_to_return;
}

struct ss_cache_slot *ss_load_to_slot(uint16_t dir_idx, uint16_t len)
{
	int idx = ss_slot_acquire(fs_dir, fs_slots, SS_MAX_ENTRIES, len);

	if (idx < 0) {
		return NULL;
	}

	struct ss_cache_slot *slot = &fs_slots[idx];
	uint8_t *buffer_to_use = NULL;
	size_t buffer_size = 0;

	if (slot->buf) {
		if (slot->_b_dirty) {
			LOG_DBG("Cache slot for key 0x%04x is dirty, writing to NVS",
				fs_dir[slot->dir_idx].key);
			nvs_write(&fs, fs_dir[slot->dir_idx].key, slot->buf, slot->_l);
		}

		if (len > slot->_b_size) {
			SS_FREE(slot->buf);
		} else {
			buffer_size = slot->_b_size;
			buffer_to_use = slot->buf;
			memset(buffer_to_use, 0, buffer_size);
		}

		slot->buf = NULL;
		slot->_b_size = 0;
		slot->_b_dirty = 0;
	}

	if (!buffer_to_use) {
		buffer_size = len;
		LOG_DBG("Allocating buffer of size %d", buffer_size);
		buffer_to_use = SS_ALLOC_N(buffer_size * sizeof(uint8_t));
	}

	if (!buffer_to_use) {
		LOG_ERR("Failed to allocate buffer of size %d", buffer_size);
		return NULL;
	}

	int rc = nvs_read(&fs, fs_dir[dir_idx].key, buffer_to_use, len);
	if (rc < 0) {
		LOG_ERR("NVS read failed: %d", rc);
		SS_FREE(buffer_to_use);
		return NULL;
	}

	slot->buf = buffer_to_use;
	slot->dir_idx = dir_idx;
	slot->_p = 0;
	slot->_l = len;
	slot->_b_size = buffer_size;
	slot->_b_dirty = 0;

	return slot;
}

char *ss_fgets(char *str, int n, ss_FILE fp)
{
	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;

	if (!slot) {
		LOG_ERR("Invalid file pointer, ss_fgets failed");
		return NULL;
	}

	if (slot->_p >= slot->_l) {
		/* No more data to read */
		return NULL;
	}

	int idx = 0; /* Destination buffer index */

	while (slot->_p < slot->_l && idx < n - 1 && slot->buf[slot->_p] != '\0' &&
	       slot->buf[slot->_p] != '\n') {
		str[idx++] = slot->buf[slot->_p++];
	}

	str[idx] = '\0';
	return str;
}

int ss_fclose(ss_FILE fp)
{
	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;

	if (!slot) {
		LOG_ERR("Invalid file pointer, ss_fclose failed");
		return -1;
	}

	uint8_t flags = fs_dir[slot->dir_idx].flags;

	if (flags & FS_READ_ONLY) {
		goto out;
	}

	if (flags & FS_COMMIT_ON_CLOSE) {
		if (slot->_b_dirty) {
			nvs_write(&fs, fs_dir[slot->dir_idx].key, slot->buf, slot->_l);
		}
		slot->_b_dirty = 0;
	}

out:
	slot->_p = 0; /* TODO: Resetting internal read/write pointer not needed? */
	return 0;
}

int ss_fseek(ss_FILE fp, long offset, int whence)
{
	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;

	if (!slot) {
		LOG_ERR("Invalid file pointer, ss_fseek failed");
		return -1;
	}

	if (whence == SEEK_SET) {
		slot->_p = offset;
	} else if (whence == SEEK_CUR) {
		slot->_p += offset;
		if (slot->_p >= slot->_l) {
			slot->_p = slot->_l - 1;
		}
	} else if (whence == SEEK_END) {
		slot->_p = slot->_l - offset;
	}

	return 0;
}

long ss_ftell(ss_FILE fp)
{
	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;

	if (!slot) {
		return -1;
	}

	return slot->_p;
}

int ss_fputc(int c, ss_FILE fp)
{
	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;

	if (!slot) {
		return -1;
	}

	if (slot->_p >= slot->_b_size) {
		uint8_t *old_buffer = slot->buf;
		size_t old_size = slot->_b_size;

		slot->buf = SS_ALLOC_N(slot->_b_size + 20);

		if (!slot->buf) {
			slot->buf = old_buffer;
			return -1;
		}

		memcpy(slot->buf, old_buffer, old_size);
		SS_FREE(old_buffer);
		slot->_b_size += 20;
	}

	slot->buf[slot->_p++] = (uint8_t)c;
	slot->_b_dirty = 1;
	slot->_l = slot->_l >= slot->_p ? slot->_l : slot->_p;

	return c;
}

int ss_access(const char *path, int amode)
{
	/* TODO: This is safe to omit for now. Internally SoftSIM will verify that a
	 * directory exists after creation. Easier to guarantee since it isn't a 'thing'
	 */

	return 0;
}

int ss_mkdir(const char *path, int mode)
{
	/* We don't care. This is a virtual filesystem, so directories
	 * are not really a thing. We just create files and directories are
	 * implicitly created.
	 */

	return 0;
}

int ss_rmdir(const char *path)
{
	/* TODO: Remove all entries with directory match */
	return 0;
}

int ss_remove(const char *path)
{
	int dir_idx = ss_dir_find(fs_dir, fs_dir_count, path);

	if (dir_idx < 0) {
		return -1;
	}

	int slot_idx = ss_slot_find(fs_slots, SS_MAX_ENTRIES, (uint16_t)dir_idx);

	if (slot_idx >= 0) {
		/* The file is going away; its buffer dies with it, dirty or not. */
		SS_FREE(fs_slots[slot_idx].buf);
		memset(&fs_slots[slot_idx], 0, sizeof(fs_slots[slot_idx]));
	}

	nvs_delete(&fs, fs_dir[dir_idx].key);

	/* Close the hole with the last entry, re-pointing its slot if buffered. */
	size_t last = fs_dir_count - 1;

	if ((size_t)dir_idx != last) {
		fs_dir[dir_idx] = fs_dir[last];

		int moved = ss_slot_find(fs_slots, SS_MAX_ENTRIES, (uint16_t)last);

		if (moved >= 0) {
			fs_slots[moved].dir_idx = (uint16_t)dir_idx;
		}
	}
	fs_dir_count--;

	return 0;
}

size_t ss_fwrite(const void *ptr, size_t size, size_t count, ss_FILE fp)
{
	struct ss_cache_slot *slot = (struct ss_cache_slot *)fp;

	if (!slot) {
		return -1;
	}

	const size_t requiredBufferSize = slot->_p + size * count;

	if (requiredBufferSize > slot->_b_size) {
		uint8_t *oldBuffer = slot->buf;
		const size_t oldSize = slot->_b_size;

		slot->buf = SS_ALLOC_N(requiredBufferSize);

		if (!slot->buf) {
			slot->buf = oldBuffer;
			return -1;
		} else {
			slot->_b_size = requiredBufferSize;
		}

		memcpy(slot->buf, oldBuffer, oldSize);
		SS_FREE(oldBuffer);
	}

	const size_t buffer_left = slot->_b_size - slot->_p;
	const size_t elements_to_copy = buffer_left > size * count ? count : buffer_left / size;

	const uint8_t content_is_different =
		memcmp(slot->buf + slot->_p, ptr, size * elements_to_copy);

	if (content_is_different) {
		memcpy(slot->buf + slot->_p, ptr, size * elements_to_copy);
		slot->_b_dirty = 1;
	}
	slot->_p += size * elements_to_copy;

	return elements_to_copy;
}

/**
 * @brief Checks if the SoftSIM is provisioned
 *
 * @return 1 if provisioned, 0 if not provisioned
 */
int port_check_provisioned(void)
{
	int ret;
	uint8_t buffer[IMSI_BIN_LEN] = {0};
	int dir_idx = ss_dir_find(fs_dir, fs_dir_count, IMSI_PATH);

	if (dir_idx < 0) {
		LOG_DBG("IMSI EF not in filesystem cache => not provisioned");
		return 0;
	}

	ret = nvs_read(&fs, fs_dir[dir_idx].key, buffer, IMSI_BIN_LEN);
	if (ret < 0) {
		return 0;
	}

	if (memcmp(buffer, default_imsi, IMSI_BIN_LEN) == 0) {
		return 0;
	}

	return 1;
}

/**
 * @brief Provisions SoftSIM with the given profile
 *
 * @param profile The profile containing the provisioning data
 *
 * @return 0 on success, -1 on failure
 */
int port_provision(struct ss_profile *profile)
{
	int rc = ss_init_fs();
	if (rc) {
		LOG_ERR("Failed to init FS");
		return -1;
	}

	/* The onomondo-uicc parser hands EF contents back as hex-ASCII and stores
	 * the real key material in A001/A004. The nrf port instead stores the
	 * compact binary form and keeps KI/KIC/KID in the KMU, so the on-flash
	 * A001/A004 carry only a one-byte KMU slot tag in place of each key (the
	 * AES/CMAC impl in ss_crypto.c resolves the tag to the hardware key). Build
	 * those binary EFs here. */
	uint8_t iccid[ICCID_BIN_LEN];
	uint8_t imsi[IMSI_BIN_LEN];
	uint8_t a001[A001_BIN_LEN] = {0};
	uint8_t a004[A004_BIN_LEN] = {0};

	hex2bin((char *)profile->_3F00_2FE2, ICCID_LEN, iccid, sizeof(iccid));
	hex2bin((char *)profile->_3F00_7ff0_6f07, IMSI_LEN, imsi, sizeof(imsi));

	/* A001: [KI_TAG | 15x 0x00 | OPC[16] | flag 0x00]. The KI slot holds only
	 * the KMU tag; OPC sits at hex offset KEY_SIZE in the parsed profile. */
	a001[0] = KI_TAG;
	hex2bin((char *)&profile->_3F00_A001[KEY_SIZE], KEY_SIZE, &a001[KEY_BIN_LEN],
		sizeof(a001) - KEY_BIN_LEN);

	/* A004: [header(6) | KIC_TAG ...(16) | KID_TAG ...(16) | 0xFF padding]. */
	static const char a004_header[] = "b00011060101";
	const size_t header_size = (sizeof(a004_header) - 1) / 2;           /* 6 */
	const size_t record_size = header_size + KEY_BIN_LEN + KEY_BIN_LEN; /* 38 */
	hex2bin((char *)a004_header, sizeof(a004_header) - 1, a004, sizeof(a004));
	memset(&a004[record_size], 0xFF, sizeof(a004) - record_size);
	a004[header_size] = KIC_TAG;
	a004[header_size + KEY_BIN_LEN] = KID_TAG;

	int dir_idx = ss_dir_find(fs_dir, fs_dir_count, IMSI_PATH);

	if (dir_idx < 0) {
		LOG_ERR("EF IMSI not in filesystem cache");
		goto out_err;
	}

	LOG_INF("Provisioning SoftSIM 1/4");
	if (nvs_write(&fs, fs_dir[dir_idx].key, imsi, IMSI_BIN_LEN) < 0) {
		goto out_err;
	}
	fs_dir[dir_idx].flags = 0;

	LOG_INF("Provisioning SoftSIM 2/4");
	dir_idx = ss_dir_find(fs_dir, fs_dir_count, ICCID_PATH);
	if (dir_idx < 0) {
		LOG_ERR("EF ICCID not in filesystem cache");
		goto out_err;
	}
	if (nvs_write(&fs, fs_dir[dir_idx].key, iccid, ICCID_BIN_LEN) < 0) {
		goto out_err;
	}
	fs_dir[dir_idx].flags = 0;

	LOG_INF("Provisioning SoftSIM 3/4");
	dir_idx = ss_dir_find(fs_dir, fs_dir_count, A001_PATH);
	if (dir_idx < 0) {
		LOG_ERR("EF A001 not in filesystem cache");
		goto out_err;
	}
	if (nvs_write(&fs, fs_dir[dir_idx].key, a001, sizeof(a001)) < 0) {
		goto out_err;
	}
	fs_dir[dir_idx].flags = 0;

	LOG_INF("Provisioning SoftSIM 4/4");
	dir_idx = ss_dir_find(fs_dir, fs_dir_count, A004_PATH);
	if (dir_idx < 0) {
		LOG_ERR("EF A004 not in filesystem cache");
		goto out_err;
	}
	if (nvs_write(&fs, fs_dir[dir_idx].key, a004, sizeof(a004)) < 0) {
		goto out_err;
	}
	fs_dir[dir_idx].flags = 0;

	/* Optionally provision EF.SMSP. The profile may carry the SMS-parameter
	 * record (profile->SMSP) and/or just the service-centre address
	 * (profile->SMSC); both are hex-ASCII and target record 1 of EF.SMSP.
	 * EF.SMSP is a fixed-size record EF, so read-modify-write to preserve
	 * its length (and any further records). */
	uint8_t zeros_smsp[SMSP_RECORD_SIZE * 2] = {0};
	uint8_t zeros_smsc[SMSC_LEN] = {0};
	int have_smsp = memcmp(profile->SMSP, zeros_smsp, sizeof(zeros_smsp)) != 0;
	int have_smsc = memcmp(profile->SMSC, zeros_smsc, sizeof(zeros_smsc)) != 0;

	if (have_smsp || have_smsc) {
		LOG_INF("Provisioning SoftSIM EF.SMSP");
		dir_idx = ss_dir_find(fs_dir, fs_dir_count, SMSP_PATH);
		if (dir_idx < 0) {
			LOG_ERR("EF.SMSP not in filesystem cache");
			goto out_err;
		}

		uint8_t smsp[SMSP_RECORD_SIZE * 2]; /* 104: holds a 2-record EF.SMSP */
		int ef_len = nvs_read(&fs, fs_dir[dir_idx].key, NULL, 0);
		if (ef_len < SMSC_REC_OFFSET + (int)(SMSC_LEN / 2) || ef_len > (int)sizeof(smsp)) {
			LOG_ERR("Unexpected EF.SMSP length: %d", ef_len);
			goto out_err;
		}
		if (nvs_read(&fs, fs_dir[dir_idx].key, smsp, ef_len) != ef_len) {
			LOG_ERR("Failed to read EF.SMSP");
			goto out_err;
		}

		/* Overlay record 1 with the SMSP, then the SMSC (so an explicit SMSC
		 * wins), each only when the profile provides it. */
		if (have_smsp) {
			hex2bin((char *)profile->SMSP, SMSP_RECORD_SIZE * 2, smsp, sizeof(smsp));
		}
		if (have_smsc) {
			hex2bin((char *)profile->SMSC, SMSC_LEN, &smsp[SMSC_REC_OFFSET],
				sizeof(smsp) - SMSC_REC_OFFSET);
		}

		if (nvs_write(&fs, fs_dir[dir_idx].key, smsp, ef_len) < 0) {
			goto out_err;
		}
		fs_dir[dir_idx].flags = 0;
	}

	LOG_INF("SoftSIM provisioned");
	return 0;

out_err:
	LOG_ERR("SoftSIM provisioning failed");
	return -1;
}
