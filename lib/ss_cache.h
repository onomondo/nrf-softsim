#ifndef _F_CACHE_H_
#define _F_CACHE_H_

#include <stdint.h>

#include <onomondo/softsim/list.h>

#define FS_READ_ONLY        (1UL << 8)
#define FS_COMMIT_ON_CLOSE  (1UL << 7) /* Commit changes to NVS on close */

struct cache_entry {
	struct ss_list list;
	uint16_t key;         /* NVS key */
	uint8_t _flags;       /* Part of ID is used for flags */
	uint16_t _p;          /* Local 'file' pointer (ftell, fseek, etc.) */
	uint16_t _l;          /* Local 'file' length */
	uint8_t *buf;         /* In case content is cached */
	uint16_t _b_size;     /* Memory allocated for buf */
	uint8_t _b_dirty;     /* Buf is divergent from NVS */
	uint8_t _cache_hits;
	char *name;           /* Path/key for lookup */
};

/**
 * @brief Find a suitable cache entry with a buffer that can be re-used
 *
 * @param entry Pointer to a cache entry
 * @param cache Pointer to a cache
 *
 * @return Pointer to a suitable cache entry, or NULL if none found
 */
struct cache_entry *f_cache_find_buffer(struct cache_entry *entry, struct ss_list *cache);

/**
 * @brief Find a cache entry by name
 *
 * @param name Name of the cache entry to find
 * @param cache Pointer to a cache
 *
 * @return Pointer to the cache entry with the given name, or NULL if not found
 */
struct cache_entry *f_cache_find_by_name(const char *name, struct ss_list *cache);

#endif /* _F_CACHE_H_ */
