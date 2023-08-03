#ifndef _F_CACHE_H_
#define _F_CACHE_H_

#include <onomondo/softsim/list.h>
#include <stdint.h>

#define FS_READ_ONLY (1UL << 8)
#define FS_COMMIT_ON_CLOSE (1UL << 7)  // commit changes to NVS on close

struct cache_entry {
  struct ss_list list;
  uint16_t key;      // NVS key
  uint8_t _flags;    // part of ID is used for flags // TODO
  uint16_t _p;       // local 'file' pointer (ftell, fseek etc)
  uint16_t _l;       // local 'file' length
  uint8_t *buf;      // in case content is cached
  uint16_t _b_size;  // memory allocated for buf
  uint8_t _b_dirty;  // buf is divergent from NVS
  uint8_t _cache_hits;
  char *name;  // path // key for lookup
};

/**
 * @brief find a suitable cache entry with a buffer that can be re-used
 *
 */
struct cache_entry *f_cache_find_buffer(struct cache_entry *entry, struct ss_list *cache);
struct cache_entry *f_cache_find_by_name(const char *name, struct ss_list *cache);
#endif /* _F_CACHE_H_ */
