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
/* Owned by the write-persistence test alone. The growth test extends
 * COMMIT_PATH's record, and with shuffle on either order is possible, so a test
 * that asserts an exact on-flash length needs a record nothing else touches. */
#define PATCH_ID   0x8011
#define PATCH_PATH "/3f00/a101"

/* Records with no flag byte at all: ss_fclose() does not commit these, so a
 * write to one stays dirty in the cache. That makes them the only way to reach
 * the two paths that write to flash outside ss_fclose() -- the eviction
 * write-back and the ss_deinit_fs() flush. */
#define EVICT_COUNT   12
#define EVICT_BASE_ID 0x0020
#define DEINIT_ID     0x0030
#define DEINIT_PATH   "/3f00/d000"

static const uint8_t plain_content[] = {0x98, 0x00, 0x10, 0x32, 0x54, 0x76, 0x98, 0x10, 0x32, 0x14};
static const uint8_t commit_content[] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t patch_content[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static const uint8_t deinit_content[] = {0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7};

/* SS_MAX_ENTRIES in ss_cache.c: the cache grows to this many buffered entries
 * before f_cache_find_buffer() starts evicting. */
#define CACHE_CAPACITY 10

/* One file bigger than every other, so the open that evicts cannot reuse the
 * victim's buffer and has to take the free-and-reallocate branch. */
#define EVICT_BIG_INDEX 10
#define EVICT_BIG_SIZE  64
#define EVICT_SIZE      8

static void evict_path(unsigned int i, char *out)
{
	snprintk(out, 11, "/3f00/e0%02u", i);
}

static size_t evict_size(unsigned int i)
{
	return (i == EVICT_BIG_INDEX) ? EVICT_BIG_SIZE : EVICT_SIZE;
}

static size_t put_record(uint8_t *buf, size_t pos, uint16_t id, const char *name)
{
	size_t len = strlen(name);

	buf[pos++] = (uint8_t)len;
	buf[pos++] = (uint8_t)(id >> 8);
	buf[pos++] = (uint8_t)(id & 0xff);
	memcpy(&buf[pos], name, len);

	return pos + len;
}

/* The suite's own view of the partition, independent of the one ss_fs.c mounts.
 * Reading through this handle is how a test proves bytes actually reached NVS
 * rather than only the in-memory cache buffer. */
static struct nvs_fs seed;

/* Read a record straight off the flash simulator.
 *
 * The re-mount is required, not hygiene: this handle and the one inside ss_fs.c
 * are two independent mounts of the same partition, so this one's write pointers
 * still describe the flash as it was before ss_fs.c wrote, and a plain nvs_read
 * happily returns the superseded copy of the record. Re-mounting re-scans the
 * sectors and picks up whatever the port has since committed. */
static ssize_t read_flash_record(uint16_t id, void *buf, size_t len)
{
	zassume_ok(nvs_mount(&seed), "could not re-mount to observe flash");

	return nvs_read(&seed, id, buf, len);
}

/* Lay down a directory table and two files, the way template.hex would.
 * zassume rather than zassert: a failing assert in a suite setup does not stop
 * the setup, so the seeding would otherwise carry on against an unmounted fs. */
static void seed_nvs(void)
{
	uint8_t blob[256];
	size_t len;

	seed.flash_device = PARTITION_DEVICE(nvs_storage);
	seed.sector_size = 0x1000;
	seed.sector_count = PARTITION_SIZE(nvs_storage) / 0x1000;
	seed.offset = PARTITION_OFFSET(nvs_storage);

	zassume_ok(nvs_mount(&seed), "could not mount the simulated NVS");
	zassume_ok(nvs_clear(&seed), "could not clear NVS");
	zassume_ok(nvs_mount(&seed), "could not re-mount after clear");

	len = put_record(blob, 0, PLAIN_ID, PLAIN_PATH);
	len = put_record(blob, len, COMMIT_ID, COMMIT_PATH);
	len = put_record(blob, len, PATCH_ID, PATCH_PATH);
	len = put_record(blob, len, DEINIT_ID, DEINIT_PATH);

	for (unsigned int i = 0; i < EVICT_COUNT; i++) {
		char path[11];

		evict_path(i, path);
		len = put_record(blob, len, (uint16_t)(EVICT_BASE_ID + i), path);
	}

	zassume_true(nvs_write(&seed, DIR_ID, blob, len) > 0, "DIR write failed");
	zassume_true(nvs_write(&seed, PLAIN_ID, plain_content, sizeof(plain_content)) > 0,
		     "seeding %s failed", PLAIN_PATH);
	zassume_true(nvs_write(&seed, COMMIT_ID, commit_content, sizeof(commit_content)) > 0,
		     "seeding %s failed", COMMIT_PATH);
	zassume_true(nvs_write(&seed, PATCH_ID, patch_content, sizeof(patch_content)) > 0,
		     "seeding %s failed", PATCH_PATH);
	zassume_true(nvs_write(&seed, DEINIT_ID, deinit_content, sizeof(deinit_content)) > 0,
		     "seeding %s failed", DEINIT_PATH);

	for (unsigned int i = 0; i < EVICT_COUNT; i++) {
		uint8_t content[EVICT_BIG_SIZE];

		memset(content, (uint8_t)(0x40 + i), sizeof(content));
		zassume_true(nvs_write(&seed, (uint16_t)(EVICT_BASE_ID + i), content,
				       evict_size(i)) > 0,
			     "seeding eviction file %u failed", i);
	}
}

static void *suite_setup(void)
{
	seed_nvs();
	zassume_ok(ss_init_fs(), "ss_init_fs() failed against a seeded partition");

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

/*
 * A re-open alone proves nothing: ss_fopen() returns early when the entry still
 * holds its buffer, so the read is served from the very buffer the write went
 * into and would pass even with the nvs_write in ss_fclose() deleted. The record
 * is therefore also read back through the suite's own NVS handle, which is the
 * only assertion here that reaches flash.
 */
ZTEST(softsim_fs, test_write_then_reread_in_the_same_session)
{
	const uint8_t patch[] = {0xaa, 0xbb, 0xcc, 0xdd};
	uint8_t buf[sizeof(patch_content)] = {0};
	ss_FILE f = ss_fopen(PATCH_PATH, "r+");

	zassert_not_null(f);
	zassert_equal(ss_fwrite(patch, 1, sizeof(patch), f), sizeof(patch));
	ss_fclose(f);

	f = ss_fopen(PATCH_PATH, "r");
	zassert_not_null(f);
	zassert_equal(ss_fread(buf, 1, sizeof(patch), f), sizeof(patch));
	zassert_mem_equal(buf, patch, sizeof(patch), "the write was not visible on re-open");
	ss_fclose(f);

	/* PATCH_ID carries FS_COMMIT_ON_CLOSE, so ss_fclose() must have written the
	 * whole record: the patched head followed by the untouched tail. */
	memset(buf, 0, sizeof(buf));
	zassert_equal(read_flash_record(PATCH_ID, buf, sizeof(buf)), sizeof(patch_content),
		      "record length on flash changed");
	zassert_mem_equal(buf, patch, sizeof(patch), "the write never reached NVS");
	zassert_mem_equal(buf + sizeof(patch), patch_content + sizeof(patch),
			  sizeof(patch_content) - sizeof(patch),
			  "the bytes past the write were not preserved on flash");
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

/* --- cache eviction and shutdown ------------------------------------------- */

/*
 * The cache holds ten buffers; past that, every open has to take one away from
 * another entry. That code writes the victim back if it was dirty, then either
 * reuses its buffer or frees and reallocates -- it is the only path that both
 * frees cache memory and writes to flash, and on a device it only runs after
 * enough files have been touched. Two files are open at once at most anywhere
 * else in this suite, so nothing else here reaches it.
 *
 * These records carry no FS_COMMIT_ON_CLOSE, so ss_fclose() leaves them dirty:
 * if a modified byte shows up on flash, the eviction write-back put it there.
 */
ZTEST(softsim_fs, test_cache_eviction_writes_the_victim_back)
{
	const uint8_t marker = 0xef;
	char path[11];
	uint8_t buf[EVICT_BIG_SIZE];
	unsigned int modified_on_flash = 0;
	ss_FILE f;

	/* Fill the cache and dirty every entry in it. */
	for (unsigned int i = 0; i < CACHE_CAPACITY; i++) {
		evict_path(i, path);
		f = ss_fopen(path, "r+");
		zassert_not_null(f, "%s did not open", path);
		zassert_equal(ss_fwrite(&marker, 1, 1, f), 1, "%s did not take the write", path);
		ss_fclose(f);
	}

	/* The cache is now full and entirely dirty, so this open must evict: the
	 * victim is written back, and because this file is larger than any victim
	 * buffer, that buffer is freed rather than handed over. */
	evict_path(EVICT_BIG_INDEX, path);
	f = ss_fopen(path, "r");
	zassert_not_null(f, "%s did not open", path);
	zassert_equal(ss_fread(buf, 1, EVICT_BIG_SIZE, f), EVICT_BIG_SIZE,
		      "the reallocated buffer did not hold a full record");
	for (size_t j = 0; j < EVICT_BIG_SIZE; j++) {
		zassert_equal(buf[j], 0x40 + EVICT_BIG_INDEX, "byte %zu came back wrong", j);
	}
	ss_fclose(f);

	for (unsigned int i = 0; i < CACHE_CAPACITY; i++) {
		zassert_equal(read_flash_record((uint16_t)(EVICT_BASE_ID + i), buf, EVICT_SIZE),
			      EVICT_SIZE, "eviction changed the length of record %u", i);
		if (buf[0] == marker) {
			modified_on_flash++;
		}
	}
	zassert_true(modified_on_flash >= 1,
		     "no dirty victim was written back; %u/%u modified records reached flash",
		     modified_on_flash, CACHE_CAPACITY);

	/* The entry evicted above is clean now and its buffer is big enough for a
	 * small file, which is the other branch: the buffer is handed over instead
	 * of being freed. */
	evict_path(EVICT_BIG_INDEX + 1, path);
	f = ss_fopen(path, "r");
	zassert_not_null(f, "%s did not open", path);
	zassert_equal(ss_fread(buf, 1, EVICT_SIZE, f), EVICT_SIZE);
	for (size_t j = 0; j < EVICT_SIZE; j++) {
		zassert_equal(buf[j], 0x40 + EVICT_BIG_INDEX + 1,
			      "a reused buffer leaked byte %zu from its previous owner", j);
	}
	ss_fclose(f);

	/* Whatever the eviction order was, no write may have been dropped. */
	for (unsigned int i = 0; i < CACHE_CAPACITY; i++) {
		evict_path(i, path);
		f = ss_fopen(path, "r");
		zassert_not_null(f, "%s did not reopen", path);
		zassert_equal(ss_fread(buf, 1, EVICT_SIZE, f), EVICT_SIZE);
		zassert_equal(buf[0], marker, "the write to %s was lost", path);
		ss_fclose(f);
	}
}

/*
 * ss_deinit_fs() is what the modem's DEINIT request reaches, and for records
 * without FS_COMMIT_ON_CLOSE it is the only thing that ever writes them to
 * flash. Until it runs, the write exists only in RAM -- which is what a power
 * loss before shutdown would throw away, and is asserted here so the boundary
 * is pinned rather than assumed.
 *
 * The suite's filesystem is torn down and brought back up in the middle of this
 * test; it re-initialises before returning so the remaining tests still have a
 * mounted filesystem whatever order they run in.
 */
ZTEST(softsim_fs, test_deinit_commits_what_close_did_not)
{
	const uint8_t marker = 0x5a;
	uint8_t buf[sizeof(deinit_content)];
	ss_FILE f = ss_fopen(DEINIT_PATH, "r+");

	zassert_not_null(f);
	zassert_equal(ss_fwrite(&marker, 1, 1, f), 1);
	ss_fclose(f);

	zassert_equal(read_flash_record(DEINIT_ID, buf, sizeof(buf)), sizeof(deinit_content));
	zassert_equal(buf[0], deinit_content[0],
		      "a record without FS_COMMIT_ON_CLOSE reached flash on close");

	zassert_ok(ss_deinit_fs(), "ss_deinit_fs() failed");
	zassert_equal(read_flash_record(DEINIT_ID, buf, sizeof(buf)), sizeof(deinit_content),
		      "the flush changed the record length");
	zassert_equal(buf[0], marker, "ss_deinit_fs() did not commit the dirty buffer");

	zassert_ok(ss_init_fs(), "the filesystem did not come back up after deinit");
	f = ss_fopen(DEINIT_PATH, "r");
	zassert_not_null(f, "%s did not open after the remount", DEINIT_PATH);
	zassert_equal(ss_fread(buf, 1, sizeof(buf), f), sizeof(buf));
	zassert_equal(buf[0], marker, "the committed byte did not survive the remount");
	zassert_mem_equal(buf + 1, deinit_content + 1, sizeof(deinit_content) - 1,
			  "the rest of the record did not survive the remount");
	ss_fclose(f);
}

/*
 * There is deliberately no test for ss_fwrite()'s identical-content check (the
 * memcmp that leaves _b_dirty clear to save a flash write): Zephyr's NVS makes
 * the same comparison against the stored record and returns without writing, so
 * flash looks identical whether the port's check is there or not. The behaviour
 * is only observable from inside the cache entry, which no test can reach.
 */

/* --- seek bounds ----------------------------------------------------------- */

/*
 * Proxy for a known defect in ss_fread(), not in ss_fseek() itself: seeking
 * past EOF is POSIX-like and intentional, but ss_fread() then computes
 * (_l - _p) unsigned, which underflows to ~65535 and turns into an
 * out-of-bounds memcpy from the cache buffer.
 *
 * Asserted on the position rather than by performing the read, because the
 * read aborts the whole process under ASan. Once ss_fread() gets its EOF
 * guard (branch fix/ss-fread-eof-short-read), replace this test and the one
 * below with direct fread-past-EOF short-read tests and drop the
 * ZTEST_EXPECT_FAIL markers.
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
 * Same proxy, other direction: SEEK_END computes (_l - offset) unsigned, so
 * overshooting the start of the file wraps the position, and a following
 * ss_fread() over-reads exactly as above.
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
