/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2026 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

#include "ss_cache.h"
#include <onomondo/softsim/mem.h>

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

/* Fixed header preceding the variable-length name in each DIR record:
 * a 1-byte name length followed by a 2-byte (big-endian) NVS key. */
#define DIR_RECORD_HEADER_LEN 3

/* The 8-byte entry is the point of this table; catch a padding surprise. */
BUILD_ASSERT(sizeof(struct ss_dir_entry) == 8, "struct ss_dir_entry must stay 8 bytes");

/* FNV-1a. Case-sensitive on purpose, matching the strcmp lookup it replaced;
 * the submodule's ss_profile_crc32() lowercases its input and would not. */
static uint32_t fnv1a(const uint8_t *data, size_t len)
{
	uint32_t hash = 2166136261u;

	for (size_t i = 0; i < len; i++) {
		hash = (hash ^ data[i]) * 16777619u;
	}

	return hash;
}

/* See in ss_cache.h */
int ss_dir_table_from_blob(const uint8_t *blob, size_t size, struct ss_dir_entry **out)
{
	size_t cursor = 0;
	size_t count = 0;

	*out = NULL;

	/* First pass: count the well-formed records, applying the same
	 * truncation rule as the fill below (a declared name that runs past
	 * the end of the blob must not be read). */
	while (cursor < size) {
		uint8_t len = blob[cursor]; /* peek the name length */

		if (cursor + DIR_RECORD_HEADER_LEN + len > size) {
			LOG_WRN("DIR blob truncated; ignoring trailing %u byte(s)",
				(unsigned)(size - cursor));
			break;
		}
		cursor += DIR_RECORD_HEADER_LEN + len;
		count++;
	}

	if (count == 0) {
		return 0;
	}

	struct ss_dir_entry *dir = SS_ALLOC_N(count * sizeof(struct ss_dir_entry));

	if (!dir) {
		LOG_ERR("Failed to allocate the directory table (%u entries)", (unsigned)count);
		return -1;
	}

	cursor = 0;
	for (size_t i = 0; i < count; i++) {
		uint8_t len = blob[cursor];
		uint16_t id = (blob[cursor + 1] << 8) | blob[cursor + 2];

		dir[i].hash = fnv1a(&blob[cursor + DIR_RECORD_HEADER_LEN], len);
		dir[i].key = id;
		dir[i].flags = (id & 0xFF00) >> 8;
		dir[i].hits = 0;
		cursor += DIR_RECORD_HEADER_LEN + len;
	}

	/* Two paths with the same hash would make lookups serve the wrong
	 * file; refuse the whole table instead. Quadratic, but only at init
	 * and only over a few hundred entries at most. */
	for (size_t i = 1; i < count; i++) {
		for (size_t j = 0; j < i; j++) {
			if (dir[i].hash == dir[j].hash) {
				LOG_ERR("DIR entries with NVS keys 0x%04x and 0x%04x share hash "
					"0x%08x; refusing the table",
					dir[j].key, dir[i].key, dir[i].hash);
				SS_FREE(dir);
				return -1;
			}
		}
	}

	*out = dir;
	return (int)count;
}

/* See in ss_cache.h */
int ss_dir_find(const struct ss_dir_entry *dir, size_t count, const char *name)
{
	uint32_t hash = fnv1a((const uint8_t *)name, strlen(name));

	for (size_t i = 0; i < count; i++) {
		if (dir[i].hash == hash) {
			return (int)i;
		}
	}

	return -1;
}

/* See in ss_cache.h */
int ss_slot_find(const struct ss_cache_slot *slots, size_t count, uint16_t dir_idx)
{
	for (size_t i = 0; i < count; i++) {
		if (slots[i].buf && slots[i].dir_idx == dir_idx) {
			return (int)i;
		}
	}

	return -1;
}

/* See in ss_cache.h */
int ss_slot_acquire(const struct ss_dir_entry *dir, const struct ss_cache_slot *slots, size_t count,
		    size_t want_len)
{
	int no_hits_no_write_existing_buff =
		-1;                /* Best case: no write needed, buffer size >= want_len */
	int no_hits_no_write = -1; /* No write needed, buffer too small */
	int no_hits = -1;          /* Write needed but low hit count */

	/* Above the hit counter's ceiling, so a fully-hit cache still yields a
	 * victim: the slot table is the capacity, there is no growing past it. */
	size_t min_hits_1 = 0x100, min_hits_2 = 0x100, min_hits_3 = 0x100;

	/* Let the cache grow to capacity before evicting anything. */
	for (size_t i = 0; i < count; i++) {
		if (!slots[i].buf) {
			return (int)i;
		}
	}

	for (size_t i = 0; i < count; i++) {
		uint8_t hits = dir[slots[i].dir_idx].hits;

		if (!slots[i]._b_dirty && slots[i]._b_size >= want_len && hits < min_hits_1) {
			min_hits_1 = hits;
			no_hits_no_write_existing_buff = (int)i;
		}
		if (!slots[i]._b_dirty && hits < min_hits_2) {
			min_hits_2 = hits;
			no_hits_no_write = (int)i;
		}
		if (hits < min_hits_3) {
			min_hits_3 = hits;
			no_hits = (int)i;
		}
	}

	if (no_hits_no_write_existing_buff >= 0) {
		return no_hits_no_write_existing_buff;
	}

	if (no_hits_no_write >= 0) {
		return no_hits_no_write;
	}

	return no_hits;
}
