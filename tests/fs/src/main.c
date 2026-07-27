/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Tests for lib/ss_fs.c -- the UICC filesystem the nRF port layers over NVS.
 *
 * This is where most of the repo's memory bugs have been: #143 (buffer growth
 * leak), #148 (negative nvs_read), #150 (NULL cache lookups). It runs against
 * the real Zephyr NVS on the flash simulator rather than a mock, because the
 * failures worth catching are in sector rotation and cache write-back.
 *
 * NVS is seeded directly before ss_init_fs() runs: the module refuses to
 * initialise against blank flash, which on a real device is why the storage
 * partition ships pre-populated from template.hex.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include <nrf_softsim.h>
#include <onomondo/softsim/fs.h>

LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

/* Part of the filesystem surface but declared in no header -- ss_fs.c only
 * defines them. Declared here so the tests can reach them. */
int ss_fputc(int c, ss_FILE fp);
long ss_ftell(ss_FILE fp);

/* DIR_ID in ss_fs.c: NVS key 1 holds the directory table. */
#define DIR_ID 1

#define PLAIN_ID    0x0002
#define PLAIN_PATH  "/3f00/2fe2"
#define COMMIT_ID   0x8010 /* high byte 0x80 -> FS_COMMIT_ON_CLOSE */
#define COMMIT_PATH "/3f00/a100"

static const uint8_t plain_content[] = {0x98, 0x00, 0x10, 0x32, 0x54, 0x76, 0x98, 0x10, 0x32, 0x14};
static const uint8_t commit_content[] = {1, 2, 3, 4, 5, 6, 7, 8};

static size_t put_record(uint8_t *buf, size_t pos, uint16_t id, const char *name)
{
	size_t len = strlen(name);

	buf[pos++] = (uint8_t)len;
	buf[pos++] = (uint8_t)(id >> 8);
	buf[pos++] = (uint8_t)(id & 0xff);
	memcpy(&buf[pos], name, len);

	return pos + len;
}

/* Lay down a directory table and two files, the way template.hex would. */
static void seed_nvs(void)
{
	static struct nvs_fs seed;
	uint8_t blob[64];
	size_t len;

	seed.flash_device = PARTITION_DEVICE(nvs_storage);
	seed.sector_size = 0x1000;
	seed.sector_count = PARTITION_SIZE(nvs_storage) / 0x1000;
	seed.offset = PARTITION_OFFSET(nvs_storage);

	zassert_ok(nvs_mount(&seed), "could not mount the simulated NVS");
	zassert_true(nvs_clear(&seed) == 0, "could not clear NVS");
	zassert_ok(nvs_mount(&seed), "could not re-mount after clear");

	len = put_record(blob, 0, PLAIN_ID, PLAIN_PATH);
	len = put_record(blob, len, COMMIT_ID, COMMIT_PATH);

	zassert_true(nvs_write(&seed, DIR_ID, blob, len) > 0, "DIR write failed");
	zassert_true(nvs_write(&seed, PLAIN_ID, plain_content, sizeof(plain_content)) > 0);
	zassert_true(nvs_write(&seed, COMMIT_ID, commit_content, sizeof(commit_content)) > 0);
}

static void *suite_setup(void)
{
	seed_nvs();
	zassert_ok(ss_init_fs(), "ss_init_fs() failed against a seeded partition");

	return NULL;
}

ZTEST_SUITE(softsim_fs, NULL, suite_setup, NULL, NULL, NULL);

/* --- the happy paths that everything else depends on ----------------------- */

ZTEST(softsim_fs, test_seeded_file_opens_with_the_right_size)
{
	ss_FILE f = ss_fopen(PLAIN_PATH, "r");

	zassert_not_null(f, "a file listed in the DIR table must open");
	zassert_equal(ss_file_size(PLAIN_PATH), sizeof(plain_content));
	ss_fclose(f);
}

ZTEST(softsim_fs, test_read_returns_the_stored_content)
{
	uint8_t buf[sizeof(plain_content)] = {0};
	ss_FILE f = ss_fopen(PLAIN_PATH, "r");

	zassert_not_null(f);
	zassert_equal(ss_fread(buf, 1, sizeof(buf), f), sizeof(buf));
	zassert_mem_equal(buf, plain_content, sizeof(buf), "content differs from what NVS holds");
	ss_fclose(f);
}

ZTEST(softsim_fs, test_unknown_path_does_not_open)
{
	zassert_is_null(ss_fopen("/3f00/dead", "r"), "a path not in the DIR table must not open");
}

ZTEST(softsim_fs, test_write_then_reread_in_the_same_session)
{
	const uint8_t patch[] = {0xaa, 0xbb, 0xcc, 0xdd};
	uint8_t buf[sizeof(patch)] = {0};
	ss_FILE f = ss_fopen(COMMIT_PATH, "r+");

	zassert_not_null(f);
	zassert_equal(ss_fwrite(patch, 1, sizeof(patch), f), sizeof(patch));
	ss_fclose(f);

	f = ss_fopen(COMMIT_PATH, "r");
	zassert_not_null(f);
	zassert_equal(ss_fread(buf, 1, sizeof(buf), f), sizeof(buf));
	zassert_mem_equal(buf, patch, sizeof(patch), "the write was not visible on re-open");
	ss_fclose(f);
}

/*
 * #143 was a missing SS_FREE when ss_fputc grew the cache buffer. Growing a
 * buffer repeatedly is the shape that leaked; under --enable-lsan the leak is
 * reported at exit rather than by this assertion, which is the point of running
 * the suite with sanitizers on.
 */
ZTEST(softsim_fs, test_repeated_buffer_growth_does_not_leak)
{
	ss_FILE f = ss_fopen(COMMIT_PATH, "r+");

	zassert_not_null(f);

	for (int i = 0; i < 200; i++) {
		zassert_true(ss_fputc('a' + (i % 26), f) >= 0, "ss_fputc failed at %d", i);
	}

	ss_fclose(f);
}

/* --- seek bounds ----------------------------------------------------------- */

/*
 * Known defect. SEEK_SET assigns the offset straight into a uint16_t position
 * with no bound, so the file pointer can be left past EOF. ss_fread() then
 * computes (_l - _p) unsigned, which underflows to ~65535 and turns into an
 * out-of-bounds memcpy from the cache buffer.
 *
 * Asserted on the resulting position rather than by performing the read, so the
 * defect is reported instead of aborting the process under ASan.
 */
ZTEST(softsim_fs, test_seek_set_cannot_position_past_eof)
{
	ss_FILE f = ss_fopen(PLAIN_PATH, "r");
	long size = ss_file_size(PLAIN_PATH);

	zassert_not_null(f);
	ss_fseek(f, size + 100, SEEK_SET);
	zassert_true(ss_ftell(f) <= size, "SEEK_SET left the position %ld bytes past EOF",
		     ss_ftell(f) - size);
	ss_fclose(f);
}
ZTEST_EXPECT_FAIL(softsim_fs, test_seek_set_cannot_position_past_eof);

/*
 * Same class: SEEK_END computes (_l - offset) unsigned, so overshooting the
 * start of the file wraps to a huge position instead of clamping to zero.
 */
ZTEST(softsim_fs, test_seek_end_cannot_wrap_before_the_start)
{
	ss_FILE f = ss_fopen(PLAIN_PATH, "r");
	long size = ss_file_size(PLAIN_PATH);

	zassert_not_null(f);
	ss_fseek(f, size + 100, SEEK_END);
	zassert_true(ss_ftell(f) >= 0 && ss_ftell(f) <= size,
		     "SEEK_END wrapped the position to %ld", ss_ftell(f));
	ss_fclose(f);
}
ZTEST_EXPECT_FAIL(softsim_fs, test_seek_end_cannot_wrap_before_the_start);

/* --- error signalling ------------------------------------------------------ */

/*
 * Known defect. ss_fwrite returns size_t but signals failure with -1, so a
 * caller comparing against the requested count sees SIZE_MAX elements written
 * rather than an error.
 */
ZTEST(softsim_fs, test_write_failure_is_not_reported_as_size_max)
{
	const uint8_t data[] = {1, 2, 3, 4};
	size_t written = ss_fwrite(data, 1, sizeof(data), NULL);

	zassert_not_equal(written, SIZE_MAX, "ss_fwrite signalled failure as SIZE_MAX");
	zassert_true(written < sizeof(data), "a failed write must report fewer elements");
}
ZTEST_EXPECT_FAIL(softsim_fs, test_write_failure_is_not_reported_as_size_max);

/*
 * There is deliberately no companion test for ss_fread(..., NULL) here.
 * ss_fwrite, ss_fseek and ss_ftell all guard their file pointer; ss_fread does
 * not, and goes straight to entry->_l, so the call segfaults rather than
 * reporting an error. A crash takes the whole binary down, which ZTEST_EXPECT_FAIL
 * cannot contain, so the case can only be added once the guard exists -- see the
 * unmerged fix/ss-fread-eof-short-read branch, which adds both that NULL check
 * and the EOF guard the seek tests above imply.
 */
