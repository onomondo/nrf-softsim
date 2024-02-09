#include <stdlib.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "f_cache.h"
#include "provision.h"
#include <onomondo/softsim/mem.h>

char storage_path[] = "";

/**
 * @brief TODO: move this function to a more appropriate place
 * It is used to generate the directory structure based on the content in the
 * "DIR" file. The DIR file encodes ID (used to locate actual file in flash) and
 * name of the file.
 *
 * @param dirs linked list to populate
 * @param blob pointer to blob of data
 * @param size size of blob
 */
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
    cursor += (len);

    struct cache_entry *entry = SS_ALLOC(struct cache_entry);
    memset(entry, 0, sizeof(struct cache_entry));

    entry->key = id;
    entry->name = name;
    entry->_flags = (id & 0xFF00) >> 8;
    entry->buf = NULL;

    ss_list_put(dirs, &entry->list);
  }
}
