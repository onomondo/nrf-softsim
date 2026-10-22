/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Unit tests for lib/ss_cache.c.
 *
 * ss_dir_table_from_blob() decodes the directory table read back from flash,
 * so it is an input-validation surface fed by potentially corrupt data; its
 * predecessor already had an out-of-bounds read fixed once (#145). The lookup
 * helpers decide which cached buffer gets evicted, which is where several of
 * the filesystem's memory bugs have originated.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "ss_cache.h"
#include <onomondo/softsim/mem.h>

/* ss_cache.c does LOG_MODULE_DECLARE(softsim, ...); register it once here. */
LOG_MODULE_REGISTER(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

/* A DIR record is [name_len | id_hi | id_lo | name[name_len]]. */
static size_t put_record(uint8_t *buf, size_t pos, uint16_t id, const char *name)
{
	size_t len = strlen(name);

	buf[pos++] = (uint8_t)len;
	buf[pos++] = (uint8_t)(id >> 8);
	buf[pos++] = (uint8_t)(id & 0xff);
	memcpy(&buf[pos], name, len);

	return pos + len;
}

ZTEST_SUITE(softsim_cache, NULL, NULL, NULL, NULL, NULL);

/* --- ss_dir_table_from_blob ------------------------------------------------- */

ZTEST(softsim_cache, test_decode_two_records)
{
	uint8_t blob[64];
	struct ss_dir_entry *dir;
	size_t len;

	len = put_record(blob, 0, 0x0002, "/3f00/2fe2");
	len = put_record(blob, len, 0x0003, "/3f00/a001");

	int n = ss_dir_table_from_blob(blob, len, &dir);

	zassert_equal(n, 2, "both records should decode");
	zassert_equal(dir[0].key, 0x0002);
	zassert_equal(ss_dir_find(dir, n, "/3f00/2fe2"), 0, "the first path must map to entry 0");
	zassert_equal(ss_dir_find(dir, n, "/3f00/a001"), 1, "the second path must map to entry 1");
	zassert_equal(dir[0].hits, 0, "a freshly decoded entry must start unopened");

	SS_FREE(dir);
}

/*
 * Regression for #145. A record whose declared name runs past the end of the
 * blob must be dropped, not read. Before that fix this walked off the end --
 * under --enable-asan the pre-fix code fails here rather than merely returning
 * a bad count.
 */
ZTEST(softsim_cache, test_truncated_tail_is_ignored)
{
	uint8_t blob[64];
	struct ss_dir_entry *dir;
	size_t len;

	len = put_record(blob, 0, 0x0002, "/3f00/2fe2");

	/* Second record claims a 32-byte name but only 4 bytes remain. */
	blob[len++] = 32;
	blob[len++] = 0x00;
	blob[len++] = 0x07;
	blob[len++] = 'x';

	int n = ss_dir_table_from_blob(blob, len, &dir);

	zassert_equal(n, 1, "truncated trailing record must be dropped");

	SS_FREE(dir);
}

ZTEST(softsim_cache, test_declared_name_longer_than_whole_blob)
{
	uint8_t blob[8] = {0xff, 0x00, 0x02, 'a', 'b', 'c', 'd', 'e'};
	struct ss_dir_entry *dir;

	int n = ss_dir_table_from_blob(blob, sizeof(blob), &dir);

	zassert_equal(n, 0, "a single over-long record must yield nothing");
	zassert_is_null(dir, "an empty result must not hand out a table");
}

/* Erased flash reads back as 0xFF; it must not decode into anything. */
ZTEST(softsim_cache, test_erased_flash_blob_decodes_to_nothing)
{
	uint8_t blob[128];
	struct ss_dir_entry *dir;

	memset(blob, 0xff, sizeof(blob));

	int n = ss_dir_table_from_blob(blob, sizeof(blob), &dir);

	zassert_equal(n, 0, "0xFF fill must not produce entries");
	zassert_is_null(dir);
}

ZTEST(softsim_cache, test_zero_length_name)
{
	uint8_t blob[16];
	struct ss_dir_entry *dir;
	size_t len;

	len = put_record(blob, 0, 0x0005, "");
	len = put_record(blob, len, 0x0006, "/3f00");

	int n = ss_dir_table_from_blob(blob, len, &dir);

	/* A zero-length record is well-formed: it consumes exactly the 3-byte
	 * header and must not desynchronise the records that follow. */
	zassert_equal(n, 2);
	zassert_equal(ss_dir_find(dir, n, ""), 0);
	zassert_equal(ss_dir_find(dir, n, "/3f00"), 1);

	SS_FREE(dir);
}

/*
 * The high byte of the DIR id carries the flags, but the id is ALSO the NVS key
 * -- entry->key keeps all 16 bits. Changing a file's flags therefore changes
 * where its data lives, so the two must stay separable.
 */
ZTEST(softsim_cache, test_flags_are_derived_but_key_keeps_all_16_bits)
{
	const uint16_t ids[] = {0x0002, 0x8010, 0x0110};
	const uint8_t expected_flags[] = {0x00, 0x80, 0x01};
	uint8_t blob[64];
	struct ss_dir_entry *dir;
	size_t len = 0;

	len = put_record(blob, len, ids[0], "/a");
	len = put_record(blob, len, ids[1], "/b");
	len = put_record(blob, len, ids[2], "/c");

	int n = ss_dir_table_from_blob(blob, len, &dir);

	zassert_equal(n, 3);

	for (size_t i = 0; i < 3; i++) {
		zassert_equal(dir[i].key, ids[i], "NVS key must keep the flag bits");
		zassert_equal(dir[i].flags, expected_flags[i], "flags are the high byte");
	}

	SS_FREE(dir);
}

/*
 * Known defect: flags is a uint8_t holding (id >> 8), so a flag macro above
 * 0xFF can never match. FS_COMMIT_ON_CLOSE (1<<7) is expressed in post-shift
 * space and works; FS_READ_ONLY (1<<8) is expressed in raw-id space and does
 * not, which makes the read-only guard in ss_fs.c dead code.
 *
 * Expected to fail until the units mismatch is fixed. Fixing it changes no
 * behaviour today -- the shipped template marks no file read-only -- but the
 * fix must not touch key derivation (see the test above).
 */
ZTEST(softsim_cache, test_flag_macros_fit_the_flags_field)
{
	zassert_true(FS_COMMIT_ON_CLOSE <= UINT8_MAX, "FS_COMMIT_ON_CLOSE cannot match");
	zassert_true(FS_READ_ONLY <= UINT8_MAX, "FS_READ_ONLY cannot match a uint8_t flags");
}
ZTEST_EXPECT_FAIL(softsim_cache, test_flag_macros_fit_the_flags_field);

/*
 * Lookups compare path hashes, so two paths hashing identically would silently
 * serve one file for the other. The table build must refuse that outright.
 * The pair below really collides under FNV-1a (both hash to 0x38902ebd);
 * found by brute force, any future hash change invalidates it loudly.
 */
ZTEST(softsim_cache, test_colliding_paths_refuse_the_table)
{
	uint8_t blob[64];
	struct ss_dir_entry *dir;
	size_t len;

	len = put_record(blob, 0, 0x0002, "/3f00/07a0f5");
	len = put_record(blob, len, 0x0003, "/3f00/0aec20");

	int n = ss_dir_table_from_blob(blob, len, &dir);

	zassert_equal(n, -1, "colliding paths must fail the whole table");
	zassert_is_null(dir, "a refused table must not be handed out");
}

/* --- ss_dir_find ------------------------------------------------------------- */

ZTEST(softsim_cache, test_find_by_name_hit_and_miss)
{
	uint8_t blob[64];
	struct ss_dir_entry *dir;
	size_t len;

	len = put_record(blob, 0, 0x0002, "/3f00/2fe2");
	len = put_record(blob, len, 0x0003, "/3f00/a001");

	int n = ss_dir_table_from_blob(blob, len, &dir);
	int hit = ss_dir_find(dir, n, "/3f00/a001");

	zassert_true(hit >= 0);
	zassert_equal(dir[hit].key, 0x0003);

	zassert_equal(ss_dir_find(dir, n, "/3f00/6f07"), -1, "miss must return -1");
	zassert_equal(ss_dir_find(dir, n, ""), -1, "empty path must not match");

	SS_FREE(dir);
}

/* --- ss_slot_acquire --------------------------------------------------------- */

/* SS_MAX_ENTRIES slots: the cache is allowed to grow to this many buffered
 * files before anything is evicted. */
#define EXPECTED_MAX_ENTRIES 10

static struct ss_dir_entry dir_pool[EXPECTED_MAX_ENTRIES + 2];
static struct ss_cache_slot pool[EXPECTED_MAX_ENTRIES + 2];
static uint8_t pool_buf[EXPECTED_MAX_ENTRIES + 2][8];

static void pool_reset(size_t buffered)
{
	memset(dir_pool, 0, sizeof(dir_pool));
	memset(pool, 0, sizeof(pool));

	for (size_t i = 0; i < buffered; i++) {
		pool[i].buf = pool_buf[i];
		pool[i]._b_size = sizeof(pool_buf[i]);
		pool[i].dir_idx = (uint16_t)i;
		dir_pool[i].hits = 5;
	}
}

ZTEST(softsim_cache, test_acquire_lets_the_cache_grow_first)
{
	pool_reset(EXPECTED_MAX_ENTRIES - 1);

	int idx = ss_slot_acquire(dir_pool, pool, EXPECTED_MAX_ENTRIES, 4);

	zassert_true(idx >= 0);
	zassert_is_null(pool[idx].buf, "below the cap nothing should be evicted");
}

ZTEST(softsim_cache, test_acquire_prefers_clean_big_enough_least_used)
{
	pool_reset(EXPECTED_MAX_ENTRIES);

	/* Make one slot the obvious victim: clean, big enough, fewest hits. */
	pool[3]._b_dirty = 0;
	dir_pool[3].hits = 0;

	/* A dirty slot with even fewer hits must NOT win -- reusing it would
	 * cost a flash write. */
	pool[7]._b_dirty = 1;
	dir_pool[7].hits = 0;

	int idx = ss_slot_acquire(dir_pool, pool, EXPECTED_MAX_ENTRIES, 4);

	zassert_equal(idx, 3, "clean slot should win over dirty");
}

/*
 * The middle preference: a clean slot whose buffer is too small still beats a
 * dirty one, because handing it over only costs an allocation while reusing a
 * dirty slot costs a flash write. Every slot in the pool is big enough for the
 * requests above, so without this case the first two preferences are
 * indistinguishable and only one of the three branches is ever taken.
 */
ZTEST(softsim_cache, test_acquire_takes_a_clean_small_buffer_over_a_dirty_one)
{
	const size_t want = sizeof(pool_buf[0]) + 8;

	pool_reset(EXPECTED_MAX_ENTRIES);

	/* Clean, fewest hits, but its buffer cannot hold the request. */
	pool[2]._b_dirty = 0;
	dir_pool[2].hits = 1;

	/* Dirty and completely unused: still loses, a write is the worse cost. */
	pool[5]._b_dirty = 1;
	dir_pool[5].hits = 0;

	/* Nobody can satisfy the size, so the best-case branch stays empty. */
	for (size_t i = 0; i < EXPECTED_MAX_ENTRIES; i++) {
		zassert_true(pool[i]._b_size < want, "slot %zu was unexpectedly big enough", i);
	}

	int idx = ss_slot_acquire(dir_pool, pool, EXPECTED_MAX_ENTRIES, want);

	zassert_equal(idx, 2, "a clean undersized buffer should win over dirty");
}

ZTEST(softsim_cache, test_acquire_falls_back_to_dirty_when_all_dirty)
{
	pool_reset(EXPECTED_MAX_ENTRIES);

	for (size_t i = 0; i < EXPECTED_MAX_ENTRIES; i++) {
		pool[i]._b_dirty = 1;
	}
	dir_pool[6].hits = 0;

	int idx = ss_slot_acquire(dir_pool, pool, EXPECTED_MAX_ENTRIES, 4);

	zassert_equal(idx, 6, "least-used dirty slot is the fallback");
}

/*
 * The hit counter saturates at 255. The list-based predecessor compared
 * against a sentinel of 100, so once every buffered file had been opened 100
 * times no victim was ever found and the cache silently grew past its
 * capacity, one buffer per new file, for the life of the session. The slot
 * table IS the capacity: a fully-hit cache must still yield a victim.
 */
ZTEST(softsim_cache, test_acquire_still_evicts_when_every_file_is_hot)
{
	pool_reset(EXPECTED_MAX_ENTRIES);

	for (size_t i = 0; i < EXPECTED_MAX_ENTRIES; i++) {
		dir_pool[i].hits = 200;
	}
	dir_pool[4].hits = 150;

	int idx = ss_slot_acquire(dir_pool, pool, EXPECTED_MAX_ENTRIES, 4);

	zassert_equal(idx, 4, "a saturated cache must still evict its least-used slot");
}
