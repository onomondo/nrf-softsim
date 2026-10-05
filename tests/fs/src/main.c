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
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include <nrf_softsim.h>
#include <onomondo/softsim/fs.h>
#include <onomondo/utils/ss_profile.h>

LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

/* Part of the filesystem surface but declared in no header -- ss_fs.c only
 * defines them. Declared here so the tests can reach them. */
int ss_fputc(int c, ss_FILE fp);
long ss_ftell(ss_FILE fp);
int port_provision(struct ss_profile *profile);
int port_check_provisioned(void);

/* Binary EF sizes and KMU slot tags, as ss_fs.c derives them from the profile
 * field sizes. Kept here rather than exported so the test states the layout it
 * expects independently of the implementation. */
#define KEY_BIN_LEN   (KEY_SIZE / 2)
#define IMSI_BIN_LEN  (IMSI_LEN / 2)
#define ICCID_BIN_LEN (ICCID_LEN / 2)
#define A001_BIN_LEN  (A001_LEN / 2)
#define A004_BIN_LEN  (A004_LEN / 2)

/* DIR_ID in ss_fs.c: NVS key 1 holds the directory table. */
#define DIR_ID 1

/* Any ordinary readable EF. Deliberately not EF.ICCID: that path is what
 * port_provision() overwrites, and the provisioning test below owns it. */
#define PLAIN_ID    0x0002
#define PLAIN_PATH  "/3f00/2f05"
#define COMMIT_ID   0x8010 /* high byte 0x80 -> FS_COMMIT_ON_CLOSE */
#define COMMIT_PATH "/3f00/a100"
/* Owned by the write-persistence test alone. The growth test extends
 * COMMIT_PATH's record, and with shuffle on either order is possible, so a test
 * that asserts an exact on-flash length needs a record nothing else touches. */
#define PATCH_ID    0x8011
#define PATCH_PATH  "/3f00/a101"

/* Records with no flag byte at all: ss_fclose() does not commit these, so a
 * write to one stays dirty in the cache. That makes them the only way to reach
 * the two paths that write to flash outside ss_fclose() -- the eviction
 * write-back and the ss_deinit_fs() flush. */
#define EVICT_COUNT   12
#define EVICT_BASE_ID 0x0020
#define DEINIT_ID     0x0030
#define DEINIT_PATH   "/3f00/d000"
#define GROW_ID       0x0050
#define GROW_PATH     "/3f00/g000"

/* Deletion targets. They are the last three records in the DIR blob, in this
 * order: the delete test needs TAIL to be the final record and DEL_A to come
 * before it. */
#define DEL_A_ID   0x0060
#define DEL_A_PATH "/3f00/dela"
#define DEL_B_ID   0x0061
#define DEL_B_PATH "/3f00/delb"
#define TAIL_ID    0x0062
#define TAIL_PATH  "/3f00/tail"

/* The four EFs port_provision() writes. The paths are the ones ss_fs.c looks up
 * by name; the keys are this suite's own. */
#define IMSI_PROV_ID    0x0040
#define IMSI_PROV_PATH  "/3f00/7ff0/6f07"
#define ICCID_PROV_ID   0x0041
#define ICCID_PROV_PATH "/3f00/2fe2"
#define A001_PROV_ID    0x0042
#define A001_PROV_PATH  "/3f00/a001"
#define A004_PROV_ID    0x0043
#define A004_PROV_PATH  "/3f00/a004"

static const uint8_t plain_content[] = {0x98, 0x00, 0x10, 0x32, 0x54, 0x76, 0x98, 0x10, 0x32, 0x14};
static const uint8_t commit_content[] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t patch_content[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static const uint8_t deinit_content[] = {0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7};
static const uint8_t delete_content[] = {0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1};
static const uint8_t tail_content[] = {0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81};

/* default_imsi in ss_fs.c: what an unprovisioned card carries, and what
 * port_check_provisioned() compares the stored IMSI against. */
static const uint8_t unprovisioned_imsi[] = {0x08, 0x09, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x10};

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
	uint8_t blob[512];
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
	len = put_record(blob, len, GROW_ID, GROW_PATH);
	len = put_record(blob, len, IMSI_PROV_ID, IMSI_PROV_PATH);
	len = put_record(blob, len, ICCID_PROV_ID, ICCID_PROV_PATH);
	len = put_record(blob, len, A001_PROV_ID, A001_PROV_PATH);
	len = put_record(blob, len, A004_PROV_ID, A004_PROV_PATH);

	for (unsigned int i = 0; i < EVICT_COUNT; i++) {
		char path[11];

		evict_path(i, path);
		len = put_record(blob, len, (uint16_t)(EVICT_BASE_ID + i), path);
	}

	/* Keep these last: the delete test relies on their order. */
	len = put_record(blob, len, DEL_A_ID, DEL_A_PATH);
	len = put_record(blob, len, DEL_B_ID, DEL_B_PATH);
	len = put_record(blob, len, TAIL_ID, TAIL_PATH);

	zassume_true(nvs_write(&seed, DIR_ID, blob, len) > 0, "DIR write failed");
	zassume_true(nvs_write(&seed, PLAIN_ID, plain_content, sizeof(plain_content)) > 0,
		     "seeding %s failed", PLAIN_PATH);
	zassume_true(nvs_write(&seed, COMMIT_ID, commit_content, sizeof(commit_content)) > 0,
		     "seeding %s failed", COMMIT_PATH);
	zassume_true(nvs_write(&seed, PATCH_ID, patch_content, sizeof(patch_content)) > 0,
		     "seeding %s failed", PATCH_PATH);
	zassume_true(nvs_write(&seed, DEINIT_ID, deinit_content, sizeof(deinit_content)) > 0,
		     "seeding %s failed", DEINIT_PATH);
	zassume_true(nvs_write(&seed, GROW_ID, commit_content, sizeof(commit_content)) > 0,
		     "seeding %s failed", GROW_PATH);

	/* The IMSI record starts out holding the placeholder ss_fs.c compares
	 * against, so port_check_provisioned() reports "not provisioned". */
	zassume_true(
		nvs_write(&seed, IMSI_PROV_ID, unprovisioned_imsi, sizeof(unprovisioned_imsi)) > 0,
		"seeding %s failed", IMSI_PROV_PATH);

	for (unsigned int i = 0; i < EVICT_COUNT; i++) {
		uint8_t content[EVICT_BIG_SIZE];

		memset(content, (uint8_t)(0x40 + i), sizeof(content));
		zassume_true(
			nvs_write(&seed, (uint16_t)(EVICT_BASE_ID + i), content, evict_size(i)) > 0,
			"seeding eviction file %u failed", i);
	}

	zassume_true(nvs_write(&seed, DEL_A_ID, delete_content, sizeof(delete_content)) > 0,
		     "seeding %s failed", DEL_A_PATH);
	zassume_true(nvs_write(&seed, DEL_B_ID, delete_content, sizeof(delete_content)) > 0,
		     "seeding %s failed", DEL_B_PATH);
	zassume_true(nvs_write(&seed, TAIL_ID, tail_content, sizeof(tail_content)) > 0,
		     "seeding %s failed", TAIL_PATH);
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
 * buffer repeatedly is the shape that leaked; the leak itself is reported by
 * LeakSanitizer at exit rather than by an assertion here, so this case only
 * means anything when the suite runs with --enable-lsan (which the workflow
 * passes -- without it twister sets detect_leaks=0 and nothing is checked).
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

/*
 * The same growth shape through ss_fwrite, which is the one that matters: no
 * caller in onomondo-uicc uses ss_fputc, while every UPDATE BINARY lands in
 * ss_fwrite. It reallocates by hand exactly as ss_fputc does, so it can leak
 * exactly as #143 did, and only this path would take a real device with it.
 */
ZTEST(softsim_fs, test_repeated_write_growth_does_not_leak)
{
	ss_FILE f = ss_fopen(GROW_PATH, "r+");

	zassert_not_null(f);

	for (int i = 0; i < 200; i++) {
		const uint8_t byte = (uint8_t)i;

		zassert_equal(ss_fwrite(&byte, 1, 1, f), 1, "ss_fwrite failed at %d", i);
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

/* --- deletion --------------------------------------------------------------- */

/*
 * ss_delete_file() is what the core's DELETE FILE handler reaches through
 * ss_storage_delete(). It has two jobs this test tells apart: forget the file
 * (lookups fail, the NVS record is gone) and leave every other file's cached
 * state intact -- above all a file whose buffer holds an uncommitted write.
 * DEL_A, DEL_B and TAIL are the last records of the DIR blob in that order, so
 * deleting DEL_A while TAIL is buffered makes the port close the gap in its
 * directory behind a live buffer.
 *
 * The cache is emptied first. A buffer left attached to the wrong directory
 * entry can still be written back under the right key if it happens to be the
 * next eviction victim, and a broken directory would then pass the re-open
 * check below by luck; from an empty cache the re-open takes a free slot and
 * the comparison means what it says.
 */
ZTEST(softsim_fs, test_delete_file_removes_it_and_leaves_its_neighbours_intact)
{
	const uint8_t marker = 0xd7;
	uint8_t buf[sizeof(tail_content)];
	ss_FILE f;

	zassert_ok(ss_deinit_fs(), "ss_deinit_fs() failed");
	zassert_ok(ss_init_fs(), "the filesystem did not come back up");

	/* TAIL: buffered and dirty; no flag byte, so ss_fclose() commits nothing. */
	f = ss_fopen(TAIL_PATH, "r+");
	zassert_not_null(f, "%s did not open", TAIL_PATH);
	zassert_equal(ss_fwrite(&marker, 1, 1, f), 1);
	ss_fclose(f);

	zassert_ok(ss_delete_file(DEL_A_PATH), "deleting %s failed", DEL_A_PATH);
	zassert_is_null(ss_fopen(DEL_A_PATH, "r"), "a deleted file must not open");
	zassert_equal(ss_file_size(DEL_A_PATH), -1, "a deleted file must have no size");
	zassert_equal(read_flash_record(DEL_A_ID, buf, sizeof(buf)), -ENOENT,
		      "the deleted record is still in NVS");

	/* The buffered neighbour must still be served from its buffer: the marker
	 * exists only there, flash still holds the seed byte. */
	f = ss_fopen(TAIL_PATH, "r");
	zassert_not_null(f, "%s did not open after a neighbour was deleted", TAIL_PATH);
	zassert_equal(ss_fread(buf, 1, sizeof(buf), f), sizeof(buf));
	zassert_equal(buf[0], marker, "%s lost its uncommitted write", TAIL_PATH);
	zassert_mem_equal(buf + 1, tail_content + 1, sizeof(buf) - 1,
			  "%s came back with foreign content", TAIL_PATH);
	ss_fclose(f);
	zassert_equal(read_flash_record(TAIL_ID, buf, sizeof(buf)), sizeof(tail_content));
	zassert_equal(buf[0], tail_content[0], "re-opening a buffered file wrote to flash");

	/* Delete a file while its buffer is live and dirty: the buffer dies with
	 * it and nothing of it reaches flash. */
	f = ss_fopen(DEL_B_PATH, "r+");
	zassert_not_null(f, "%s did not open", DEL_B_PATH);
	zassert_equal(ss_fwrite(&marker, 1, 1, f), 1);
	ss_fclose(f);
	zassert_ok(ss_delete_file(DEL_B_PATH), "deleting %s failed", DEL_B_PATH);
	zassert_is_null(ss_fopen(DEL_B_PATH, "r"), "a deleted buffered file must not open");
	zassert_equal(ss_file_size(DEL_B_PATH), -1);
	zassert_equal(read_flash_record(DEL_B_ID, buf, sizeof(buf)), -ENOENT,
		      "the deleted buffered record is still in NVS");

	zassert_equal(ss_delete_file("/3f00/dead"), -1, "deleting an unknown path must fail");

	/* Shutdown flushes TAIL's dirty buffer under TAIL's key. The DIR blob is
	 * never rewritten, so the remount brings the deleted paths back as records
	 * whose NVS keys are gone: they must read as absent and must not stop the
	 * filesystem from coming up. */
	zassert_ok(ss_deinit_fs(), "ss_deinit_fs() failed");
	zassert_equal(read_flash_record(TAIL_ID, buf, sizeof(buf)), sizeof(tail_content));
	zassert_equal(buf[0], marker, "the flush did not reach %s", TAIL_PATH);

	zassert_ok(ss_init_fs(), "the filesystem did not come back up after the deletions");
	f = ss_fopen(TAIL_PATH, "r");
	zassert_not_null(f, "%s did not open after the remount", TAIL_PATH);
	zassert_equal(ss_fread(buf, 1, sizeof(buf), f), sizeof(buf));
	zassert_equal(buf[0], marker, "the committed byte did not survive the remount");
	ss_fclose(f);
	zassert_is_null(ss_fopen(DEL_A_PATH, "r"), "%s came back after the remount", DEL_A_PATH);
	zassert_is_null(ss_fopen(DEL_B_PATH, "r"), "%s came back after the remount", DEL_B_PATH);
	zassert_equal(ss_file_size(DEL_A_PATH), -1);
}

/*
 * The core's storage backend also calls ss_delete_dir() and ss_create_dir().
 * Directories have no storage of their own in this port, so both are no-ops
 * that report success; what this pins is that the symbols resolve at all. They
 * are weak declarations in the core, and an undefined one is a call to address
 * zero.
 */
ZTEST(softsim_fs, test_directory_hooks_are_harmless_no_ops)
{
	zassert_ok(ss_delete_dir("/3f00/7ff0"), "ss_delete_dir must report success");
	zassert_ok(ss_create_dir("/3f00/7ff1", 0700), "ss_create_dir must report success");
}

/*
 * There is deliberately no test for ss_fwrite()'s identical-content check (the
 * memcmp that leaves _b_dirty clear to save a flash write): Zephyr's NVS makes
 * the same comparison against the stored record and returns without writing, so
 * flash looks identical whether the port's check is there or not. The behaviour
 * is only observable from inside the cache entry, which no test can reach.
 */

/* --- provisioning ---------------------------------------------------------- */

/*
 * port_provision() is where a profile turns into the four EFs the card
 * authenticates from, and it is the largest single function in the port. It was
 * faked in every suite, so none of the encodings below had ever been checked:
 * the hex-ASCII fields of the parsed profile become packed binary, and each key
 * is replaced by a one-byte KMU slot tag because the real key material lives in
 * the KMU rather than on flash.
 *
 * The KMU is not involved here -- that is nrf_softsim_provision()'s half of the
 * job, covered in the handler suite -- so this needs no PSA.
 */
ZTEST(softsim_fs, test_provisioning_writes_the_expected_binary_efs)
{
	/* The GSMA TS.48 test profile, as the parser would hand it over:
	 * hex-ASCII throughout, OPc all '0'. */
	static const char imsi_hex[] = "080910101032547698";
	static const char iccid_hex[] = "98001032547698103214";
	static const char key_hex[] = "000102030405060708090A0B0C0D0E0F";
	struct ss_profile profile = {0};
	uint8_t buf[A004_BIN_LEN];

	memcpy(profile._3F00_7ff0_6f07, imsi_hex, IMSI_LEN);
	memcpy(profile._3F00_2FE2, iccid_hex, ICCID_LEN);
	memcpy(profile._3F00_A001, key_hex, KEY_SIZE);        /* KI  */
	memset(&profile._3F00_A001[KEY_SIZE], '0', KEY_SIZE); /* OPc */

	zassert_equal(port_check_provisioned(), 0, "the seeded IMSI is the default one");
	zassert_ok(port_provision(&profile), "provisioning failed");
	zassert_equal(port_check_provisioned(), 1, "a provisioned IMSI must be detected");

	/* IMSI and ICCID are stored packed, two hex chars per byte. */
	zassert_equal(read_flash_record(IMSI_PROV_ID, buf, IMSI_BIN_LEN), IMSI_BIN_LEN);
	zassert_mem_equal(buf, "\x08\x09\x10\x10\x10\x32\x54\x76\x98", IMSI_BIN_LEN,
			  "IMSI encoding");
	zassert_equal(read_flash_record(ICCID_PROV_ID, buf, ICCID_BIN_LEN), ICCID_BIN_LEN);
	zassert_mem_equal(buf, "\x98\x00\x10\x32\x54\x76\x98\x10\x32\x14", ICCID_BIN_LEN,
			  "ICCID encoding");

	/* A001: the KI slot carries its KMU tag and nothing else, then the OPc. */
	zassert_equal(read_flash_record(A001_PROV_ID, buf, A001_BIN_LEN), A001_BIN_LEN);
	zassert_equal(buf[0], KI_TAG, "A001 does not start with the KI slot tag");
	for (size_t i = 1; i < KEY_BIN_LEN; i++) {
		zassert_equal(buf[i], 0x00, "A001 byte %zu leaked key material", i);
	}
	for (size_t i = 0; i < KEY_BIN_LEN; i++) {
		zassert_equal(buf[KEY_BIN_LEN + i], 0x00, "the all-zero OPc was mangled at %zu", i);
	}

	/* A004: a fixed 6-byte header, then the KIC and KID slot tags, then 0xff. */
	zassert_equal(read_flash_record(A004_PROV_ID, buf, A004_BIN_LEN), A004_BIN_LEN);
	zassert_mem_equal(buf, "\xb0\x00\x11\x06\x01\x01", 6, "A004 header changed");
	zassert_equal(buf[6], KIC_TAG, "A004 does not carry the KIC slot tag");
	zassert_equal(buf[6 + KEY_BIN_LEN], KID_TAG, "A004 does not carry the KID slot tag");
	zassert_equal(buf[6 + 2 * KEY_BIN_LEN], 0xff, "A004 padding does not start at 38");
	zassert_equal(buf[A004_BIN_LEN - 1], 0xff, "A004 is not padded to its full length");
}

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
