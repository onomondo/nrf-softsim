#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "ss_cache.h"
#include <onomondo/softsim/mem.h>

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_NRF_LOG_LEVEL);

#define SS_MAX_ENTRIES 10

/* Fixed header preceding the variable-length name in each DIR record:
 * a 1-byte name length followed by a 2-byte (big-endian) NVS key. */
#define DIR_RECORD_HEADER_LEN 3

/* See in ss_cache.h */
struct cache_entry *f_cache_find_buffer(struct cache_entry *entry, struct ss_list *cache)
{
	struct cache_entry *cursor;
	struct cache_entry *no_hits_no_write_existing_buff =
		NULL; /* Best case: no write needed, existing buffer with size >= min_buf_size */
	struct cache_entry *no_hits_no_write =
		NULL; /* No write needed, existing buffer with size < min_buf_size */
	struct cache_entry *no_hits = NULL; /* Write needed but low hit count */

	size_t cached_entries = 0;
	size_t min_buf_size = entry->_l;
	size_t min_hits_1 = 100, min_hits_2 = 100, min_hits_3 = 100;

	SS_LIST_FOR_EACH(cache, cursor, struct cache_entry, list)
	{
		if (cursor->buf) {
			if (!cursor->_b_dirty && cursor->_b_size >= min_buf_size &&
			    cursor->_cache_hits < min_hits_1) {
				min_hits_1 = cursor->_cache_hits;
				no_hits_no_write_existing_buff = cursor;
			}
			if (!cursor->_b_dirty && cursor->_cache_hits < min_hits_2) {
				min_hits_2 = cursor->_cache_hits;
				no_hits_no_write = cursor;
			}
			if (cursor->_cache_hits < min_hits_3) {
				min_hits_3 = cursor->_cache_hits;
				no_hits = cursor;
			}
			cached_entries++;
		}
	}

	/* Let cache grow to SS_MAX_ENTRIES */
	if (cached_entries < SS_MAX_ENTRIES) {
		return NULL;
	}

	if (no_hits_no_write_existing_buff) {
		return no_hits_no_write_existing_buff;
	}

	if (no_hits_no_write) {
		return no_hits_no_write;
	}

	if (no_hits) {
		return no_hits;
	}

	return NULL;
}

/* See in ss_cache.h */
struct cache_entry *f_cache_find_by_name(const char *name, struct ss_list *cache)
{
	struct cache_entry *cursor;

	SS_LIST_FOR_EACH(cache, cursor, struct cache_entry, list)
	{
		if (strcmp(cursor->name, name) == 0) {
			return cursor;
		}
	}

	return NULL;
}

/* See in ss_cache.h */
void generate_dir_table_from_blob(struct ss_list *dirs, uint8_t *blob, size_t size)
{
	size_t cursor = 0;

	while (cursor < size) {
		uint8_t len = blob[cursor]; /* peak the name length */
		/* Check if the record header and name fit in the remaining blob */
		if (cursor + DIR_RECORD_HEADER_LEN + len > size) {
			LOG_WRN("DIR blob truncated; ignoring trailing %u byte(s)",
				(unsigned)(size - cursor));
			break;
		}
		cursor++;

		uint16_t id = (blob[cursor] << 8) | blob[cursor + 1];

		cursor += 2;

		char *name = SS_ALLOC_N(len + 1);
		memcpy(name, &blob[cursor], len);
		name[len] = '\0';
		cursor += len;

		struct cache_entry *entry = SS_ALLOC(struct cache_entry);
		memset(entry, 0, sizeof(struct cache_entry));

		entry->key = id;
		entry->name = name;
		entry->_flags = (id & 0xFF00) >> 8;
		entry->buf = NULL;

		ss_list_put(dirs, &entry->list);
	}
}
