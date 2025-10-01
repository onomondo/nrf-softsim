#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "ss_cache.h"
#include "ss_provision.h"
#include <onomondo/softsim/mem.h>

char storage_path[] = "";

/* See in ss_provision.h */
void generate_dir_table_from_blob(struct ss_list *dirs, uint8_t *blob, size_t size)
{
	size_t cursor = 0;

	while (cursor < size) {
		uint8_t len = blob[cursor++];
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
