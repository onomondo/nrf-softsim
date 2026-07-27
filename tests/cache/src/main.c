/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Unit tests for lib/ss_cache.c.
 *
 * generate_dir_table_from_blob() decodes the directory table read back from
 * flash, so it is an input-validation surface fed by potentially corrupt data;
 * it already had an out-of-bounds read fixed once (#145). The lookup helpers
 * decide which cached buffer gets evicted, which is where several of the
 * filesystem's memory bugs have originated.
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

static size_t dir_count(struct ss_list *dirs)
{
	struct cache_entry *cursor;
	size_t n = 0;

	SS_LIST_FOR_EACH(dirs, cursor, struct cache_entry, list)
	{
		n++;
	}

	return n;
}

static void dir_free(struct ss_list *dirs)
{
	while (!ss_list_empty(dirs)) {
		struct cache_entry *entry = SS_LIST_GET_NEXT(dirs, struct cache_entry, list);

		ss_list_remove(&entry->list);
		SS_FREE(entry->name);
		SS_FREE(entry);
	}
}

ZTEST_SUITE(softsim_cache, NULL, NULL, NULL, NULL, NULL);

/* --- generate_dir_table_from_blob ------------------------------------------ */

ZTEST(softsim_cache, test_decode_two_records)
{
	uint8_t blob[64];
	struct ss_list dirs;
	size_t len;

	ss_list_init(&dirs);
	len = put_record(blob, 0, 0x0002, "/3f00/2fe2");
	len = put_record(blob, len, 0x0003, "/3f00/a001");

	generate_dir_table_from_blob(&dirs, blob, len);

	zassert_equal(dir_count(&dirs), 2, "both records should decode");

	struct cache_entry *first = SS_LIST_GET_NEXT(&dirs, struct cache_entry, list);

	zassert_equal(first->key, 0x0002);
	zassert_str_equal(first->name, "/3f00/2fe2");
	zassert_is_null(first->buf, "a freshly decoded entry must not claim a buffer");

	dir_free(&dirs);
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
	struct ss_list dirs;
	size_t len;

	ss_list_init(&dirs);
	len = put_record(blob, 0, 0x0002, "/3f00/2fe2");

	/* Second record claims a 32-byte name but only 4 bytes remain. */
	blob[len++] = 32;
	blob[len++] = 0x00;
	blob[len++] = 0x07;
	blob[len++] = 'x';

	generate_dir_table_from_blob(&dirs, blob, len);

	zassert_equal(dir_count(&dirs), 1, "truncated trailing record must be dropped");

	dir_free(&dirs);
}

ZTEST(softsim_cache, test_declared_name_longer_than_whole_blob)
{
	uint8_t blob[8] = {0xff, 0x00, 0x02, 'a', 'b', 'c', 'd', 'e'};
	struct ss_list dirs;

	ss_list_init(&dirs);
	generate_dir_table_from_blob(&dirs, blob, sizeof(blob));

	zassert_equal(dir_count(&dirs), 0, "a single over-long record must yield nothing");

	dir_free(&dirs);
}

/* Erased flash reads back as 0xFF; it must not decode into anything. */
ZTEST(softsim_cache, test_erased_flash_blob_decodes_to_nothing)
{
	uint8_t blob[128];
	struct ss_list dirs;

	memset(blob, 0xff, sizeof(blob));
	ss_list_init(&dirs);

	generate_dir_table_from_blob(&dirs, blob, sizeof(blob));

	zassert_equal(dir_count(&dirs), 0, "0xFF fill must not produce entries");

	dir_free(&dirs);
}

ZTEST(softsim_cache, test_zero_length_name)
{
	uint8_t blob[16];
	struct ss_list dirs;
	size_t len;

	ss_list_init(&dirs);
	len = put_record(blob, 0, 0x0005, "");
	len = put_record(blob, len, 0x0006, "/3f00");

	generate_dir_table_from_blob(&dirs, blob, len);

	/* A zero-length record is well-formed: it consumes exactly the 3-byte
	 * header and must not desynchronise the records that follow. */
	zassert_equal(dir_count(&dirs), 2);

	struct cache_entry *first = SS_LIST_GET_NEXT(&dirs, struct cache_entry, list);

	zassert_str_equal(first->name, "");

	dir_free(&dirs);
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
	struct ss_list dirs;
	size_t len = 0;
	size_t i = 0;

	ss_list_init(&dirs);
	len = put_record(blob, len, ids[0], "/a");
	len = put_record(blob, len, ids[1], "/b");
	len = put_record(blob, len, ids[2], "/c");

	generate_dir_table_from_blob(&dirs, blob, len);
	zassert_equal(dir_count(&dirs), 3);

	struct cache_entry *cursor;

	SS_LIST_FOR_EACH(&dirs, cursor, struct cache_entry, list)
	{
		zassert_equal(cursor->key, ids[i], "NVS key must keep the flag bits");
		zassert_equal(cursor->_flags, expected_flags[i], "flags are the high byte");
		i++;
	}

	dir_free(&dirs);
}

/*
 * Known defect: _flags is a uint8_t holding (id >> 8), so a flag macro above
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
	zassert_true(FS_READ_ONLY <= UINT8_MAX, "FS_READ_ONLY cannot match a uint8_t _flags");
}
ZTEST_EXPECT_FAIL(softsim_cache, test_flag_macros_fit_the_flags_field);

/* --- f_cache_find_by_name -------------------------------------------------- */

ZTEST(softsim_cache, test_find_by_name_hit_and_miss)
{
	uint8_t blob[64];
	struct ss_list dirs;
	size_t len;

	ss_list_init(&dirs);
	len = put_record(blob, 0, 0x0002, "/3f00/2fe2");
	len = put_record(blob, len, 0x0003, "/3f00/a001");
	generate_dir_table_from_blob(&dirs, blob, len);

	struct cache_entry *hit = f_cache_find_by_name("/3f00/a001", &dirs);

	zassert_not_null(hit);
	zassert_equal(hit->key, 0x0003);

	zassert_is_null(f_cache_find_by_name("/3f00/6f07", &dirs), "miss must return NULL");
	zassert_is_null(f_cache_find_by_name("", &dirs), "empty path must not match");

	dir_free(&dirs);
}

/* --- f_cache_find_buffer --------------------------------------------------- */

/* SS_MAX_ENTRIES in ss_cache.c; the cache is allowed to grow to this many
 * buffered entries before anything is evicted. */
#define EXPECTED_MAX_ENTRIES 10

static struct cache_entry pool[EXPECTED_MAX_ENTRIES + 2];
static uint8_t pool_buf[EXPECTED_MAX_ENTRIES + 2][8];

static void pool_reset(struct ss_list *cache, size_t buffered)
{
	ss_list_init(cache);
	memset(pool, 0, sizeof(pool));

	for (size_t i = 0; i < buffered; i++) {
		pool[i].buf = pool_buf[i];
		pool[i]._b_size = sizeof(pool_buf[i]);
		pool[i]._cache_hits = 5;
		ss_list_put(cache, &pool[i].list);
	}
}

ZTEST(softsim_cache, test_find_buffer_lets_the_cache_grow_first)
{
	struct ss_list cache;
	struct cache_entry want = {._l = 4};

	pool_reset(&cache, EXPECTED_MAX_ENTRIES - 1);

	zassert_is_null(f_cache_find_buffer(&want, &cache),
			"below the cap nothing should be evicted");
}

ZTEST(softsim_cache, test_find_buffer_prefers_clean_big_enough_least_used)
{
	struct ss_list cache;
	struct cache_entry want = {._l = 4};

	pool_reset(&cache, EXPECTED_MAX_ENTRIES);

	/* Make one entry the obvious victim: clean, big enough, fewest hits. */
	pool[3]._b_dirty = 0;
	pool[3]._cache_hits = 0;

	/* A dirty entry with even fewer hits must NOT win -- reusing it would
	 * cost a flash write. */
	pool[7]._b_dirty = 1;
	pool[7]._cache_hits = 0;

	struct cache_entry *victim = f_cache_find_buffer(&want, &cache);

	zassert_equal_ptr(victim, &pool[3], "clean entry should win over dirty");
}

ZTEST(softsim_cache, test_find_buffer_falls_back_to_dirty_when_all_dirty)
{
	struct ss_list cache;
	struct cache_entry want = {._l = 4};

	pool_reset(&cache, EXPECTED_MAX_ENTRIES);

	for (size_t i = 0; i < EXPECTED_MAX_ENTRIES; i++) {
		pool[i]._b_dirty = 1;
	}
	pool[6]._cache_hits = 0;

	struct cache_entry *victim = f_cache_find_buffer(&want, &cache);

	zassert_equal_ptr(victim, &pool[6], "least-used dirty entry is the fallback");
}

/* --- ABI invariant --------------------------------------------------------- */

/*
 * SS_LIST_GET_NEXT does pointer arithmetic on a struct ss_list *, so the
 * offsetof() it subtracts is scaled by sizeof(struct ss_list). That only
 * happens to work because the list member sits first in struct cache_entry.
 * Reorder the struct and the whole cache corrupts silently.
 */
ZTEST(softsim_cache, test_list_member_is_first_in_cache_entry)
{
	zassert_equal(offsetof(struct cache_entry, list), 0,
		      "SS_LIST_GET_NEXT relies on this being zero");
}
