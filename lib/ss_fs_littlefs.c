/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * LittleFS storage backend for the SoftSIM UICC file tree -- selected in
 * place of ss_fs.c + ss_cache.c by CONFIG_SOFTSIM_STORAGE_LITTLEFS (see the
 * SOFTSIM_STORAGE_BACKEND choice in Kconfig). Mutually exclusive with the NVS
 * backend at build time; both target the same flash partition (nvs_storage /
 * littlefs_storage label the identical devicetree node).
 *
 * Unlike NVS, LittleFS is a real hierarchical filesystem, so by default the
 * UICC path ("/3f00/7ff0/6f07" etc.) maps directly onto it -- there is no
 * NVS-style numeric-key directory-table cache here (compare ss_cache.c):
 * every EF/DF is an ordinary littlefs file or directory under the mount
 * point, and onomondo-uicc's storage_compact.c already builds and passes
 * those absolute paths straight through to ss_fopen()/ss_create_dir()/etc.
 *
 * CONFIG_ALT_FILE_SEPARATOR flips storage_compact.c's PATH_SEPARATOR to "_"
 * instead of "/", so every EF/DF instead lands as one flat, directory-less
 * entry at the mount root (e.g. "_3f00_7ff0_6f07"). This file's own path
 * macros and storage_path prefix below must be built the same way, or a
 * template built with one setting won't be visible under the other.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#include <onomondo/softsim/fs.h>
#include <onomondo/softsim/log.h>
#include <onomondo/softsim/mem.h>
#include <onomondo/softsim/storage.h>
#include <onomondo/softsim/utils.h>
#include <onomondo/utils/ss_profile.h>

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

#define LITTLEFS_PARTITION storage_partition

#define SOFTSIM_LFS_MNT_POINT "/lfs"

#ifdef CONFIG_ALT_FILE_SEPARATOR
/* Flat, directory-less naming: PATH_SEPARATOR ("_" here, see storage.h) never
 * supplies the "/" that would otherwise separate the mount point from the
 * first path element, so storage_path must carry its own trailing "/". */
#define SOFTSIM_LFS_STORAGE_PATH_INIT SOFTSIM_LFS_MNT_POINT "/"
/* No real UICC subtree to open/recurse into under flat naming -- every EF/DF
 * is a sibling entry directly under the mount point, so that's the walk
 * root (see nrf_softsim_dump_fs()/lfs_dump_dir()). */
#define SOFTSIM_LFS_ROOT_DIR SOFTSIM_LFS_MNT_POINT
/* Root DF's definition file; its presence is how ss_init_fs() tells a
 * provisioned filesystem from a freshly-formatted, empty one. There is no
 * real root directory to stat under this scheme. */
#define SOFTSIM_LFS_PROVISIONED_MARKER SOFTSIM_LFS_MNT_POINT "/_3f00.def"

#define IMSI_PATH  "_3f00_7ff0_6f07"
#define ICCID_PATH "_3f00_2fe2"
#define A001_PATH  "_3f00_a001"
#define A004_PATH  "_3f00_a004"
#define SMSP_PATH  "_3f00_7ff0_6f42"
#else
#define SOFTSIM_LFS_STORAGE_PATH_INIT SOFTSIM_LFS_MNT_POINT
/* Root DF of the UICC file tree; its presence is how ss_init_fs() tells a
 * provisioned filesystem from a freshly-formatted, empty one. */
#define SOFTSIM_LFS_ROOT_DIR SOFTSIM_LFS_MNT_POINT "/3f00"
#define SOFTSIM_LFS_PROVISIONED_MARKER SOFTSIM_LFS_ROOT_DIR

#define IMSI_PATH  "/3f00/7ff0/6f07"
#define ICCID_PATH "/3f00/2fe2"
#define A001_PATH  "/3f00/a001"
#define A004_PATH  "/3f00/a004"
#define SMSP_PATH  "/3f00/7ff0/6f42"
#endif

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
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

static uint8_t __aligned(4) softsim_lfs_read_buffer[CONFIG_FS_LITTLEFS_CACHE_SIZE];
static uint8_t __aligned(4) softsim_lfs_prog_buffer[CONFIG_FS_LITTLEFS_CACHE_SIZE];
static uint32_t softsim_lfs_lookahead_buffer[CONFIG_FS_LITTLEFS_LOOKAHEAD_SIZE / sizeof(uint32_t)];

static struct fs_littlefs softsim_lfs_data = {
	.cfg = {
		.read_size = CONFIG_FS_LITTLEFS_READ_SIZE,
		.prog_size = CONFIG_FS_LITTLEFS_PROG_SIZE,
		.cache_size = CONFIG_FS_LITTLEFS_CACHE_SIZE,
		.lookahead_size = CONFIG_FS_LITTLEFS_LOOKAHEAD_SIZE,
		.block_cycles = CONFIG_FS_LITTLEFS_BLOCK_CYCLES,
		.read_buffer = softsim_lfs_read_buffer,
		.prog_buffer = softsim_lfs_prog_buffer,
		.lookahead_buffer = softsim_lfs_lookahead_buffer,
	},
};

static struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &softsim_lfs_data,
	.storage_dev = (void *)PARTITION_ID(LITTLEFS_PARTITION),
	.mnt_point = SOFTSIM_LFS_MNT_POINT,
};

static uint8_t fs_is_initialized;
static uint8_t default_imsi[] = {0x08, 0x09, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x10};

/* Path prefix onomondo-uicc's compact-storage backend (storage_compact.c)
 * prepends to every host path -- here, the littlefs mount point, so its
 * absolute UICC paths land directly on the mounted filesystem. Declared
 * extern in <onomondo/softsim/storage.h>. */
char storage_path[SS_STORAGE_PATH_MAX] = SOFTSIM_LFS_STORAGE_PATH_INIT;

/* ss_fopen() handle: owns the Zephyr VFS file object backing an ss_FILE. */
struct ss_lfs_file {
	struct fs_file_t zfp;
};

int ss_init_fs(void)
{
	int rc;
	struct fs_dirent entry;

	if (!fs_is_initialized) {
		rc = fs_mount(&lfs_mnt);
		if (rc < 0) {
			LOG_ERR("Failed to mount littlefs (%d)", rc);
			return -1;
		}
		LOG_INF("littlefs mount succeed");
		fs_is_initialized = 1;
	}

	if (fs_stat(SOFTSIM_LFS_PROVISIONED_MARKER, &entry) < 0) {
		LOG_WRN("No UICC file tree on littlefs; starting with an empty filesystem");
		return 1;
	}

	return 0;
}

int ss_deinit_fs(void)
{
	if (!fs_is_initialized) {
		return 0;
	}

	fs_unmount(&lfs_mnt);
	fs_is_initialized = 0;

	return 0;
}

static fs_mode_t lfs_flags_from_mode(const char *mode)
{
	if (strcmp(mode, "r") == 0) {
		return FS_O_READ;
	} else if (strcmp(mode, "r+") == 0) {
		return FS_O_RDWR;
	} else if (strcmp(mode, "w") == 0) {
		return FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC;
	} else if (strcmp(mode, "w+") == 0) {
		return FS_O_RDWR | FS_O_CREATE | FS_O_TRUNC;
	} else if (strcmp(mode, "a") == 0) {
		return FS_O_WRITE | FS_O_CREATE | FS_O_APPEND;
	}

	return 0;
}

ss_FILE ss_fopen(char *path, char *mode)
{
	struct ss_lfs_file *f;
	fs_mode_t flags;
	int rc;

	if (!path || !mode) {
		return NULL;
	}

	flags = lfs_flags_from_mode(mode);
	if (flags == 0) {
		LOG_ERR("Unsupported fopen mode '%s'", mode);
		return NULL;
	}

	f = SS_ALLOC(struct ss_lfs_file);
	if (!f) {
		return NULL;
	}

	fs_file_t_init(&f->zfp);

	rc = fs_open(&f->zfp, path, flags);
	if (rc < 0) {
		LOG_INF("ss_fopen: MISS '%s' mode '%s' (%d)", path, mode, rc);
		SS_FREE(f);
		return NULL;
	}
	LOG_INF("ss_fopen: HIT  '%s' mode '%s'", path, mode);

	return (ss_FILE)f;
}

int ss_fclose(ss_FILE fp)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;
	int rc;

	if (!f) {
		LOG_ERR("Invalid file pointer, ss_fclose failed");
		return -1;
	}

	rc = fs_close(&f->zfp);
	SS_FREE(f);

	return rc;
}

size_t ss_fread(void *ptr, size_t size, size_t nmemb, ss_FILE fp)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;
	ssize_t rc;

	if (!f || size == 0 || nmemb == 0) {
		return 0;
	}

	rc = fs_read(&f->zfp, ptr, size * nmemb);
	if (rc < 0) {
		LOG_ERR("littlefs read failed: %d", (int)rc);
		return 0;
	}

	return (size_t)rc / size;
}

size_t ss_fwrite(const void *ptr, size_t size, size_t count, ss_FILE fp)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;
	ssize_t rc;

	if (!f || size == 0 || count == 0) {
		return 0;
	}

	rc = fs_write(&f->zfp, ptr, size * count);
	if (rc < 0) {
		LOG_ERR("littlefs write failed: %d", (int)rc);
		return 0;
	}

	return (size_t)rc / size;
}

int ss_fseek(ss_FILE fp, long offset, int whence)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;

	if (!f) {
		LOG_ERR("Invalid file pointer, ss_fseek failed");
		return -1;
	}

	return fs_seek(&f->zfp, offset, whence);
}

long ss_ftell(ss_FILE fp)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;

	if (!f) {
		return -1;
	}

	return fs_tell(&f->zfp);
}

int ss_fputc(int c, ss_FILE fp)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;
	uint8_t byte = (uint8_t)c;

	if (!f) {
		return -1;
	}

	if (fs_write(&f->zfp, &byte, 1) != 1) {
		return -1;
	}

	return c;
}

int ss_file_size(const char *path)
{
	struct fs_dirent entry;
	int rc = fs_stat(path, &entry);

	if (rc < 0) {
		return -1;
	}

	return (int)entry.size;
}

int ss_access(const char *path, int amode)
{
	struct fs_dirent entry;

	ARG_UNUSED(amode);

	return (fs_stat(path, &entry) == 0) ? 0 : -1;
}

int ss_create_dir(const char *path, uint32_t mode)
{
	int rc;

	ARG_UNUSED(mode);

	rc = fs_mkdir(path);
	if (rc == -EEXIST) {
		return 0;
	}

	return rc;
}

int ss_delete_file(const char *path)
{
	return fs_unlink(path);
}


int ss_delete_dir(const char *path)
{
	return fs_unlink(path);
}

static int lfs_write_ef(const char *rel_path, const uint8_t *data, size_t len)
{
	char abs_path[SS_STORAGE_PATH_MAX];
	ss_FILE fd;
	size_t written;
	int rc;

	rc = snprintf(abs_path, sizeof(abs_path), "%s%s", storage_path, rel_path);
	if (rc < 0 || (size_t)rc >= sizeof(abs_path)) {
		return -1;
	}

	fd = ss_fopen(abs_path, "r+");
	if (!fd) {
		LOG_ERR("EF %s not present on littlefs", rel_path);
		return -1;
	}

	written = ss_fwrite(data, 1, len, fd);
	ss_fclose(fd);

	return (written == len) ? 0 : -1;
}

int port_check_provisioned(void)
{
	char abs_path[SS_STORAGE_PATH_MAX];
	uint8_t buffer[IMSI_BIN_LEN] = {0};
	ss_FILE fd;
	int rc;

	rc = snprintf(abs_path, sizeof(abs_path), "%s%s", storage_path, IMSI_PATH);
	if (rc < 0 || (size_t)rc >= sizeof(abs_path)) {
		return 0;
	}

	fd = ss_fopen(abs_path, "r");
	if (!fd) {
		LOG_DBG("IMSI EF not on littlefs => not provisioned");
		return 0;
	}

	rc = (int)ss_fread(buffer, 1, sizeof(buffer), fd);
	ss_fclose(fd);
	if (rc != sizeof(buffer)) {
		return 0;
	}

	if (memcmp(buffer, default_imsi, IMSI_BIN_LEN) == 0) {
		return 0;
	}

	return 1;
}

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

	LOG_INF("Provisioning SoftSIM 1/4");
	if (lfs_write_ef(IMSI_PATH, imsi, sizeof(imsi)) < 0) {
		goto out_err;
	}

	LOG_INF("Provisioning SoftSIM 2/4");
	if (lfs_write_ef(ICCID_PATH, iccid, sizeof(iccid)) < 0) {
		goto out_err;
	}

	LOG_INF("Provisioning SoftSIM 3/4");
	if (lfs_write_ef(A001_PATH, a001, sizeof(a001)) < 0) {
		goto out_err;
	}

	LOG_INF("Provisioning SoftSIM 4/4");
	if (lfs_write_ef(A004_PATH, a004, sizeof(a004)) < 0) {
		goto out_err;
	}

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
		char smsp_path[SS_STORAGE_PATH_MAX];
		uint8_t smsp[SMSP_RECORD_SIZE * 2]; /* 104: holds a 2-record EF.SMSP */
		ss_FILE fd;
		int ef_len;

		LOG_INF("Provisioning SoftSIM EF.SMSP");

		rc = snprintf(smsp_path, sizeof(smsp_path), "%s%s", storage_path, SMSP_PATH);
		if (rc < 0 || (size_t)rc >= sizeof(smsp_path)) {
			goto out_err;
		}

		ef_len = ss_file_size(smsp_path);
		if (ef_len < SMSC_REC_OFFSET + (int)(SMSC_LEN / 2) || ef_len > (int)sizeof(smsp)) {
			LOG_ERR("Unexpected EF.SMSP length: %d", ef_len);
			goto out_err;
		}

		fd = ss_fopen(smsp_path, "r+");
		if (!fd) {
			LOG_ERR("EF.SMSP not present on littlefs");
			goto out_err;
		}

		if (ss_fread(smsp, 1, ef_len, fd) != (size_t)ef_len) {
			LOG_ERR("Failed to read EF.SMSP");
			ss_fclose(fd);
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

		ss_fseek(fd, 0, SEEK_SET);
		if (ss_fwrite(smsp, 1, ef_len, fd) != (size_t)ef_len) {
			LOG_ERR("Failed to write EF.SMSP");
			ss_fclose(fd);
			goto out_err;
		}
		ss_fclose(fd);
	}

	LOG_INF("SoftSIM provisioned");
	return 0;

out_err:
	LOG_ERR("SoftSIM provisioning failed");
	return -1;
}