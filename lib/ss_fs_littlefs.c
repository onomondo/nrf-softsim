/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * LittleFS storage backend for the SoftSIM UICC file tree -- selected in
 * place of ss_fs.c + ss_cache.c by CONFIG_SOFTSIM_FS_LFS (see the
 * SOFTSIM_FS_BACKEND choice in Kconfig). Mutually exclusive with the NVS
 * backend at build time; both target the same flash partition (nvs_storage /
 * lfs_storage label the identical devicetree node).
 *
 * There is no NVS-style numeric-key directory-table cache here (compare
 * ss_cache.c): every EF/DF is an ordinary littlefs file at the mount point,
 * and onomondo-uicc's storage_compact.c builds and passes those absolute
 * paths straight through to ss_fopen()/ss_create_dir()/etc.
 *
 * Paths are flat: CONFIG_ALT_FILE_SEPARATOR (always on with this backend,
 * see SOFTSIM_UICC_ALT_FILE_SEPARATOR in Kconfig) makes storage_compact.c
 * join the FID chain with "_" instead of "/", so every EF/DF is a single
 * directory-less entry at the mount root (e.g. "/lfs/_3f00_7ff0_6f07"). The
 * template image (lib/profile/lfs_SoftSIM.bin) is built the same way.
 *
 * The mount never auto-formats (FS_MOUNT_FLAG_NO_FORMAT): a partition
 * without a valid filesystem fails ss_init_fs() instead of being replaced
 * by an empty one.
 */

#include <errno.h>
#include <stdbool.h>
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

#define LITTLEFS_PARTITION lfs_storage

#define SOFTSIM_LFS_MNT_POINT "/lfs"

/* This port and lfs_SoftSIM.bin only implement flat "_"-separated names; with
 * "/" separators the UICC would look up paths that are not on the filesystem.
 * Kconfig ties SOFTSIM_UICC_ALT_FILE_SEPARATOR to SOFTSIM_FS_LFS, this catches
 * a build that bypasses it. */
#ifndef CONFIG_ALT_FILE_SEPARATOR
#error "SoftSIM LittleFS backend requires CONFIG_ALT_FILE_SEPARATOR (flat UICC paths)"
#endif

#define SOFTSIM_LFS_STORAGE_PATH_INIT  SOFTSIM_LFS_MNT_POINT "/"
#define SOFTSIM_LFS_ROOT_DIR           SOFTSIM_LFS_MNT_POINT
#define SOFTSIM_LFS_PROVISIONED_MARKER SOFTSIM_LFS_MNT_POINT "/_3f00.def"

#define IMSI_PATH  "_3f00_7ff0_6f07"
#define ICCID_PATH "_3f00_2fe2"
#define A001_PATH  "_3f00_a001"
#define A004_PATH  "_3f00_a004"
#define SMSP_PATH  "_3f00_7ff0_6f42"

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

/* Geometry of the mount, matched to the offline image builder (uicc/littlefs/mkfs/main.c).
 * Defaults, overridable from the compiler command line (-DSOFTSIM_LFS_CACHE_SIZE=...).
 * They are deliberately not the CONFIG_FS_LITTLEFS_* symbols: Kconfig always defines
 * those (with its own defaults), so an #ifndef on them would never take effect.
 */
#ifndef SOFTSIM_LFS_READ_SIZE
#define SOFTSIM_LFS_READ_SIZE 256
#endif
#ifndef SOFTSIM_LFS_PROG_SIZE
#define SOFTSIM_LFS_PROG_SIZE 256
#endif
#ifndef SOFTSIM_LFS_CACHE_SIZE
#define SOFTSIM_LFS_CACHE_SIZE 256
#endif
#ifndef SOFTSIM_LFS_LOOKAHEAD_SIZE
#define SOFTSIM_LFS_LOOKAHEAD_SIZE 32
#endif
#ifndef SOFTSIM_LFS_BLOCK_CYCLES
#define SOFTSIM_LFS_BLOCK_CYCLES 512
#endif

/* Zephyr's littlefs_fs.c sizes its per-file cache pool from CONFIG_FS_LITTLEFS_CACHE_SIZE
 * (a different compilation unit, so it cannot follow the macros above): with the default 64
 * every ss_fopen() would fail with -ENOMEM.
 */
BUILD_ASSERT(CONFIG_FS_LITTLEFS_CACHE_SIZE >= SOFTSIM_LFS_CACHE_SIZE,
	     "SoftSIM LFS: set CONFIG_FS_LITTLEFS_CACHE_SIZE >= SOFTSIM_LFS_CACHE_SIZE (256)");

static uint8_t __aligned(4) softsim_lfs_read_buffer[SOFTSIM_LFS_CACHE_SIZE];
static uint8_t __aligned(4) softsim_lfs_prog_buffer[SOFTSIM_LFS_CACHE_SIZE];
static uint32_t softsim_lfs_lookahead_buffer[SOFTSIM_LFS_LOOKAHEAD_SIZE / sizeof(uint32_t)];

static struct fs_littlefs softsim_lfs_data = {
	.cfg =
		{
			.read_size = SOFTSIM_LFS_READ_SIZE,
			.prog_size = SOFTSIM_LFS_PROG_SIZE,
			.cache_size = SOFTSIM_LFS_CACHE_SIZE,
			.lookahead_size = SOFTSIM_LFS_LOOKAHEAD_SIZE,
			.block_cycles = SOFTSIM_LFS_BLOCK_CYCLES,
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
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
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
	/* Opened "r+": existing content is readable, so ss_fwrite() can skip
	 * unchanged bytes. Write-only handles must not be read (littlefs asserts). */
	bool can_cmp;
};

static int lfs_log_mount_info(void)
{
	struct fs_statvfs sbuf;
	unsigned long used;
	int rc;

	rc = fs_statvfs(SOFTSIM_LFS_MNT_POINT, &sbuf);
	if (rc < 0) {
		LOG_ERR("littlefs statvfs failed (%d)", rc);
		return -1;
	}

	used = sbuf.f_blocks - sbuf.f_bfree;
	LOG_INF("littlefs geometry: block_size=%lu block_count=%lu (%lu B), used=%lu free=%lu "
		"blocks",
		sbuf.f_frsize, sbuf.f_blocks, sbuf.f_frsize * sbuf.f_blocks, used, sbuf.f_bfree);

	return (int)used;
}

int ss_init_fs(void)
{
	struct fs_dirent entry;
	int used;
	int rc;

	if (!fs_is_initialized) {
		rc = fs_mount(&lfs_mnt);
		if (rc < 0) {
			LOG_ERR("Failed to mount littlefs at %s (%d): no valid filesystem on the "
				"partition; was the SoftSIM template (merged.hex) flashed?",
				SOFTSIM_LFS_MNT_POINT, rc);
			return -1;
		}
		LOG_INF("littlefs mounted");
		fs_is_initialized = 1;
	}

	used = lfs_log_mount_info();

	/* A valid filesystem without the UICC root means the wrong template (e.g.
	 * built for the other path separator mode) or an empty one. */
	if (fs_stat(SOFTSIM_LFS_PROVISIONED_MARKER, &entry) < 0) {
		LOG_WRN("No UICC file tree ('%s' missing, %d block(s) used): template empty or "
			"built for the other path separator mode?",
			SOFTSIM_LFS_PROVISIONED_MARKER, used);
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
	f->can_cmp = (strcmp(mode, "r+") == 0);

	rc = fs_open(&f->zfp, path, flags);
	if (rc < 0) {
		LOG_DBG("ss_fopen: MISS '%s' mode '%s' (%d)", path, mode, rc);
		SS_FREE(f);
		return NULL;
	}
	LOG_DBG("ss_fopen: HIT  '%s' mode '%s'", path, mode);

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

	if ((f->zfp.flags & FS_O_READ) == 0) {
		LOG_ERR("ss_fread on a handle not opened for reading");
		return 0;
	}

	rc = fs_read(&f->zfp, ptr, size * nmemb);
	if (rc < 0) {
		LOG_ERR("littlefs read failed: %d", (int)rc);
		return 0;
	}

	return (size_t)rc / size;
}

/* Compare the bytes about to be written with what is already on flash at the
 * current position and skip the leading part that is identical: a littlefs
 * write that changes nothing still costs a program + metadata commit. Returns
 * how many leading bytes of buf already match, leaving the file position just
 * after them. Past EOF (appends, e.g. the 0xFF pre-fill) nothing matches.
 * Returns a negative errno if the file position cannot be restored, since
 * writing from the wrong offset would corrupt the file. */
#define SS_LFS_CMP_CHUNK 32

static ssize_t lfs_skip_unchanged(struct fs_file_t *zfp, const uint8_t *buf, size_t len)
{
	uint8_t cur[SS_LFS_CMP_CHUNK];
	size_t done = 0;

	while (done < len) {
		size_t n = MIN(sizeof(cur), len - done);
		ssize_t rc = fs_read(zfp, cur, n);

		if (rc < 0) {
			return done;
		}
		if ((size_t)rc == n && memcmp(cur, &buf[done], n) == 0) {
			done += n;
			continue;
		}

		/* Mismatch (or EOF) in this chunk: keep its identical prefix,
		 * rewind to the first differing byte so the caller writes from there. */
		size_t same = 0;

		while (same < (size_t)rc && cur[same] == buf[done + same]) {
			same++;
		}
		if (same < (size_t)rc) {
			int seek_rc = fs_seek(zfp, -(off_t)((size_t)rc - same), FS_SEEK_CUR);

			if (seek_rc < 0) {
				LOG_ERR("littlefs seek failed: %d", seek_rc);
				return seek_rc;
			}
		}
		return (ssize_t)(done + same);
	}

	return (ssize_t)done;
}

size_t ss_fwrite(const void *ptr, size_t size, size_t count, ss_FILE fp)
{
	struct ss_lfs_file *f = (struct ss_lfs_file *)fp;
	const uint8_t *buf = ptr;
	size_t total = size * count;
	ssize_t skipped;
	size_t skip;
	ssize_t rc;

	if (!f || size == 0 || count == 0) {
		return 0;
	}

	if ((f->zfp.flags & FS_O_WRITE) == 0) {
		LOG_ERR("ss_fwrite on a handle not opened for writing");
		return 0;
	}

	skipped = f->can_cmp ? lfs_skip_unchanged(&f->zfp, buf, total) : 0;
	if (skipped < 0) {
		return 0;
	}
	skip = (size_t)skipped;
	if (skip == total) {
		return count;
	}

	rc = fs_write(&f->zfp, &buf[skip], total - skip);
	if (rc < 0) {
		LOG_ERR("littlefs write failed: %d", (int)rc);
		return 0;
	}

	return (skip + (size_t)rc) / size;
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

	if ((f->zfp.flags & FS_O_WRITE) == 0) {
		LOG_ERR("ss_fputc on a handle not opened for writing");
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

/* The EFs written by port_provision() are fixed-size files laid down by the template image.
 * Check that the template holds each one at exactly the size about to be written, so a template
 * built for another layout is rejected before any of them is touched. */
static int lfs_check_ef_size(const char *rel_path, size_t len)
{
	char abs_path[SS_STORAGE_PATH_MAX];
	int rc = snprintf(abs_path, sizeof(abs_path), "%s%s", storage_path, rel_path);
	int size;

	if (rc < 0 || (size_t)rc >= sizeof(abs_path)) {
		return -1;
	}

	size = ss_file_size(abs_path);
	if (size < 0) {
		LOG_ERR("EF %s not present on littlefs", rel_path);
		return -1;
	}
	if ((size_t)size != len) {
		LOG_ERR("EF %s is %d B on littlefs, expected %zu B (template mismatch?)", rel_path,
			size, len);
		return -1;
	}

	return 0;
}

static int lfs_write_ef(const char *rel_path, const uint8_t *data, size_t len)
{
	char abs_path[SS_STORAGE_PATH_MAX];
	ss_FILE fd;
	size_t written;
	int rc;
	int close_rc;

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
	/* littlefs commits on close, so a failed commit only shows up here. */
	close_rc = ss_fclose(fd);
	if (close_rc < 0) {
		LOG_ERR("EF %s: close failed (%d)", rel_path, close_rc);
	}

	return (written == len && close_rc >= 0) ? 0 : -1;
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

	/* Validate the template before writing anything, so a mismatch leaves the files untouched.
	 */
	if (lfs_check_ef_size(IMSI_PATH, sizeof(imsi)) < 0 ||
	    lfs_check_ef_size(ICCID_PATH, sizeof(iccid)) < 0 ||
	    lfs_check_ef_size(A001_PATH, sizeof(a001)) < 0 ||
	    lfs_check_ef_size(A004_PATH, sizeof(a004)) < 0) {
		goto out_err;
	}

	/* The IMSI is written last: port_check_provisioned() takes a non-default IMSI as
	 * "provisioned", so it is the commit marker. An interruption before it leaves the device
	 * unprovisioned instead of provisioned with a mix of new and template data. */
	LOG_INF("Provisioning SoftSIM 1/4");
	if (lfs_write_ef(ICCID_PATH, iccid, sizeof(iccid)) < 0) {
		goto out_err;
	}

	LOG_INF("Provisioning SoftSIM 2/4");
	if (lfs_write_ef(A001_PATH, a001, sizeof(a001)) < 0) {
		goto out_err;
	}

	LOG_INF("Provisioning SoftSIM 3/4");
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

		if (ss_fseek(fd, 0, SEEK_SET) != 0) {
			LOG_ERR("Failed to rewind EF.SMSP");
			ss_fclose(fd);
			goto out_err;
		}
		if (ss_fwrite(smsp, 1, ef_len, fd) != (size_t)ef_len) {
			LOG_ERR("Failed to write EF.SMSP");
			ss_fclose(fd);
			goto out_err;
		}
		if (ss_fclose(fd) < 0) {
			LOG_ERR("Failed to commit EF.SMSP");
			goto out_err;
		}
	}

	LOG_INF("Provisioning SoftSIM 4/4");
	if (lfs_write_ef(IMSI_PATH, imsi, sizeof(imsi)) < 0) {
		goto out_err;
	}

	LOG_INF("SoftSIM provisioned");
	return 0;

out_err:
	LOG_ERR("SoftSIM provisioning failed");
	return -1;
}
