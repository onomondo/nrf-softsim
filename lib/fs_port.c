#include <stdlib.h>

#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "f_cache.h"
#include "provision.h"
#include "profile.h"
#include <onomondo/softsim/fs_port.h>
#include <onomondo/softsim/list.h>
#include <onomondo/softsim/utils.h>
#include <onomondo/softsim/log.h>
#include <onomondo/softsim/mem.h>

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_LOG_LEVEL);

#define NVS_PARTITION nvs_storage
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define DIR_ID (1UL)

#define IMSI_PATH   "/3f00/7ff0/6f07"
#define ICCID_PATH  "/3f00/2fe2"
#define A001_PATH   "/3f00/a001"
#define A004_PATH   "/3f00/a004"

#ifndef SEEK_SET
#define SEEK_SET 0 /* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1 /* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define SEEK_END 2 /* set file offset to EOF plus offset */
#endif

static struct nvs_fs fs;
static struct ss_list fs_cache;

uint8_t fs_is_initialized = 0;

// internal functions

// that the buffer is set. Either by allocating new memory or by
// stealing from another entry.
void ss_read_nvs_to_cache(struct cache_entry *entry);

int ss_init_fs() 
{
  if (fs_is_initialized) return 0;  // already initialized

  ss_list_init(&fs_cache);
  uint8_t *data = NULL;
  size_t len = 0;

  fs.flash_device = NVS_PARTITION_DEVICE;
  fs.sector_size = 0x1000;  // where to read this? :DT_PROP(NVS_PARTITION,  erase_block_size);  //<0x1000>
  fs.sector_count = FLASH_AREA_SIZE(nvs_storage) / fs.sector_size;
  fs.offset = NVS_PARTITION_OFFSET;

  int rc = nvs_mount(&fs);
  if (rc) {
    LOG_ERR("failed to mount nvs\n");
    return -1;
  }

  rc = nvs_read(&fs, DIR_ID, NULL, 0);

  len = rc;

  /*************************
   * Read DIR_ENTRY from NVS
   * This is used to construct a linked list that
   * serves as a cache and lookup table for the filesystem
   */
  if (!data && rc) {
    data = SS_ALLOC_N(len * sizeof(uint8_t));
    rc = nvs_read(&fs, DIR_ID, data, len);
    __ASSERT_NO_MSG(rc == len);
  }

  ss_list_init(&fs_cache);
  generate_dir_table_from_blob(&fs_cache, data, len);

  if (ss_list_empty(&fs_cache)) goto out;

  fs_is_initialized++;

out:
  SS_FREE(data);
  return ss_list_empty(&fs_cache);
}

int ss_deinit_fs() 
{
  // TODO: check if DIR entry is still valid
  // if not recreate and write.
  // Will NVS only commit if there are changes? If so, we can just recreate
  // and let NVS do the compare.

  struct cache_entry *cursor, *pre_cursor;

  // Free all memory allocated by cache and commit changes to NVS
  SS_LIST_FOR_EACH_SAVE(&fs_cache, cursor, pre_cursor, struct cache_entry, list) {
    if (cursor->_b_dirty) {
      LOG_INF("Softsim stop - committing %s to NVS", cursor->name);
      nvs_write(&fs, cursor->key, cursor->buf, cursor->_l);
    }

    ss_list_remove(&cursor->list);

    if (cursor->buf) {
      SS_FREE(cursor->buf);
    }
    SS_FREE(cursor->name);
    SS_FREE(cursor);
  }

  fs_is_initialized = 0;

  return 0;
}

/**
 * @brief Implements a version of standard C fopen.
 * 
 * @param path Full path.
 * @param mode Currently ignorred.
 * @return Pointer to a "file" represented by a struct cache_entry internally.
 */
port_FILE port_fopen(char *path, char *mode) 
{
  struct cache_entry *cursor = NULL;
  int rc = 0;

  cursor = f_cache_find_by_name(path, &fs_cache);
  if (!cursor) {
    return NULL;
  }

  /**
   * Currently not used.
   * Could potentially be used in the future to re-arrange order.
   * Initial order is ordered by frequency already so not big optimizations can
   * be achieved.
   */
  if (cursor->_cache_hits < 0xFF) cursor->_cache_hits++;

  /**
   * File opened first time.
   */
  if (!cursor->_l) {
    rc = nvs_read(&fs, cursor->key, NULL, 0);

    if (rc < 0) {  // TODO: this can not happen.
      return NULL;
    } else {
      cursor->_l = rc;
    }
  }

  // Reset internal read/write pointer
  cursor->_p = 0;

  // Guarantee buffer contains valid data
  ss_read_nvs_to_cache(cursor);
  return (void *)cursor;
}

/**
 * @brief Implements a version of standard C fread. Internally it will use a
 * cache.
 *
 * @param ptr destinanion memory
 * @param size size of element
 * @param nmemb number of elements
 * @param fp file pointer
 * @return elements read
 */
size_t port_fread(void *ptr, size_t size, size_t nmemb, port_FILE fp) 
{
  if (nmemb == 0 || size == 0) {
    return 0;
  }
  struct cache_entry *entry = (struct cache_entry *)fp;

  size_t max_element_to_return = (entry->_l - entry->_p) / size;
  size_t element_to_return = nmemb > max_element_to_return ? max_element_to_return : nmemb;

  memcpy(ptr, entry->buf + entry->_p, element_to_return * size);

  entry->_p += element_to_return * size;  // move internal read/write pointer
  return element_to_return;
}

/**
 * @brief Makes sure internal buffer points to the actual data by fetching it
 * from NVS or protected storage if needed.
 * 
 * @param entry Pointer to a cache entry.
 */
void ss_read_nvs_to_cache(struct cache_entry *entry) 
{
  struct cache_entry *tmp;

  /**
   * If entry has a buffer assigned it contains valid data.
   * Return early.
   */
  if (entry->buf) return;

  // best bet for a buffer we can reuse
  tmp = f_cache_find_buffer(entry, &fs_cache);

  uint8_t *buffer_to_use = NULL;
  size_t buffer_size = 0;

  if (tmp) {
    if (tmp->_b_dirty) {
      LOG_DBG("Cache entry %s is dirty, writing to NVS\n", tmp->name);
      nvs_write(&fs, tmp->key, tmp->buf, tmp->_l);
    }

    // should buffer be freed
    if (entry->_l > tmp->_b_size) {
      SS_FREE(tmp->buf);
    } else {  // or reused
      buffer_size = tmp->_b_size;
      buffer_to_use = tmp->buf;
      memset(buffer_to_use, 0, buffer_size);
    }

    tmp->buf = NULL;
    tmp->_b_size = 0;
    tmp->_b_dirty = 0;
  }

  if (!buffer_to_use) {
    buffer_size = entry->_l;
    LOG_DBG("Allocating buffer of size %d", buffer_size);
    buffer_to_use = SS_ALLOC_N(buffer_size * sizeof(uint8_t));
  }

  if (!buffer_to_use) {
    LOG_ERR("Failed to allocate buffer of size %d", buffer_size);
    return;
  }

  int rc = 0;

  rc = nvs_read(&fs, entry->key, buffer_to_use, entry->_l);

  if (rc < 0) {
    LOG_ERR("NVS read failed: %d\n", rc);
    SS_FREE(buffer_to_use);
    return;
  }

  entry->buf = buffer_to_use;
  entry->_b_size = buffer_size;
  entry->_b_dirty = 0;
}

char *port_fgets(char *str, int n, port_FILE fp) {
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return NULL;
  }

  if (entry->_p >= entry->_l) {  // no more data
    return NULL;
  }

  int idx = 0;  // dest idx

  while (entry->_p < entry->_l && idx < n - 1 && entry->buf[entry->_p] != '\0' && entry->buf[entry->_p] != '\n') {
    str[idx++] = entry->buf[entry->_p++];
  }

  str[idx] = '\0';
  return str;
}

int port_fclose(port_FILE fp) 
{
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return -1;
  }

  if (entry->_flags & FS_READ_ONLY) {
    goto out;
  }

  if (entry->_flags & FS_COMMIT_ON_CLOSE) {
    if (entry->_b_dirty) {
      nvs_write(&fs, entry->key, entry->buf, entry->_l);
    }
    entry->_b_dirty = 0;
  }

out:
  entry->_p = 0;  // TODO: not needed?
  return 0;
}

int port_fseek(port_FILE fp, long offset, int whence) 
{
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return -1;
  }

  if (whence == SEEK_SET) {
    entry->_p = offset;
  } else if (whence == SEEK_CUR) {
    entry->_p += offset;
    if (entry->_p >= entry->_l) entry->_p = entry->_l - 1;  // how is std c behaving here?
  } else if (whence == SEEK_END) {
    entry->_p = entry->_l - offset;
  }
  return 0;
}

long port_ftell(port_FILE fp) 
{
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return -1;
  }

  return entry->_p;
}

int port_fputc(int c, port_FILE fp) 
{
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return -1;
  }

  // writing beyond the end of the buffer
  if (entry->_p >= entry->_b_size) {
    uint8_t *old_buffer = entry->buf;
    size_t old_size = entry->_b_size;
    entry->buf = SS_ALLOC_N(entry->_b_size + 20);

    if (!entry->buf) {
      entry->buf = old_buffer;
      return -1;
    }

    memcpy(entry->buf, old_buffer, old_size);
    entry->_b_size += 20;
  }

  entry->buf[entry->_p++] = (uint8_t)c;
  entry->_b_dirty = 1;
  entry->_l = entry->_l >= entry->_p ? entry->_l : entry->_p;

  return c;
}

int port_access(const char *path, int amode) 
{
  return 0;
}  // TODO -> safe to omit for now. Internally SoftSIM will verify that a
   // directory exists after creation. Easier to guarentee since it isn't a
   // 'thing'

int port_mkdir(const char *, int) 
{
  return 0;
}  // don't care. We don't really obey directories (creating file
   // 'test/a/b/c.def) implicitly creates the directories

int port_remove(const char *path) 
{
  struct cache_entry *entry = f_cache_find_by_name(path, &fs_cache);

  if (!entry) {
    return -1;
  }

  ss_list_remove(&entry->list);  // doesn't free data

  // should we be smarter about this?
  nvs_delete(&fs, entry->key);

  if (entry->buf) SS_FREE(entry->buf);

  SS_FREE(entry->name);
  SS_FREE(entry);

  return 0;
}

// list for each { is_name_partial_match? {remove port_remove(cursor->name)} }
int port_rmdir(const char *) { return 0; }  // todo. Remove all entries with directory match.

static uint8_t default_imsi[] = {0x08, 0x09, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x10};

int port_check_provisioned() 
{
  int ret;
  uint8_t buffer[IMSI_LEN] = {0};
  struct cache_entry *entry = (struct cache_entry *)f_cache_find_by_name(IMSI_PATH, &fs_cache);

  ret = nvs_read(&fs, entry->key, buffer, IMSI_LEN);
  if (ret < 0) {
    return 0;
  }

  // IMSI read is still the default one from the template
  if (memcmp(buffer, default_imsi, IMSI_LEN) == 0) {
    return 0;
  }

  return 1;
}

/**
 * @brief Provision the SoftSIM with the given profile
 *
 * @param profile ptr to the profile
 * @param len Len of profile. 332 otherwise invalid.
 */
int port_provision(struct ss_profile *profile) 
{
  int rc = ss_init_fs();
  if (rc) {
    LOG_ERR("Failed to init FS");
  }

  // IMSI 6f07
  struct cache_entry *entry = (struct cache_entry *)f_cache_find_by_name(IMSI_PATH, &fs_cache);

  LOG_INF("Provisioning SoftSIM 1/4");
  if (nvs_write(&fs, entry->key, profile->IMSI, IMSI_LEN) < 0) goto out_err;
  entry->_flags = 0;

  LOG_INF("Provisioning SoftSIM 2/4");
  entry = (struct cache_entry *)f_cache_find_by_name(ICCID_PATH, &fs_cache);
  if (nvs_write(&fs, entry->key, profile->ICCID, ICCID_LEN) < 0) {
    goto out_err;
  }
  entry->_flags = 0;

  LOG_INF("Provisioning SoftSIM 3/4");
  entry = (struct cache_entry *)f_cache_find_by_name(A001_PATH, &fs_cache);
  if (nvs_write(&fs, entry->key, profile->A001, sizeof(profile->A001)) < 0) goto out_err;
  entry->_flags = 0;

  LOG_INF("Provisioning SoftSIM 4/4");
  entry = (struct cache_entry *)f_cache_find_by_name(A004_PATH, &fs_cache);
  if (nvs_write(&fs, entry->key, profile->A004, sizeof(profile->A004)) < 0) goto out_err;
  entry->_flags = 0;

  // This is for test now. Removes uicc suspend flag as it isn't supported *yet* in softsim
  // Modem seems to 'deactivate' sim more often instead which isn't good :/

  // Enable the following to remove the uicc suspend flag

  // entry = (struct cache_entry *)f_cache_find_by_name("/3f00/2f08", &fs_cache);
  // if (nvs_write(&fs, entry->key, "0a05000000", 10) < 0) goto out_err;
  // entry->_flags = 0;

  LOG_INF("SoftSIM provisioned");
  return 0;

out_err:
  LOG_ERR("SoftSIM provisioning failed");
  return -1;
}

size_t port_fwrite(const void *prt, size_t size, size_t count, port_FILE f) 
{
  struct cache_entry *entry = (struct cache_entry *)f;

  if (!entry) {
    return -1;
  }

  const size_t requiredBufferSize = entry->_p + size * count;

  // In reality this rarely occurs. Potentially when OTA updating the SIM
  if (requiredBufferSize > entry->_b_size) {
    uint8_t *oldBuffer = entry->buf;
    const size_t oldSize = entry->_b_size;

    entry->buf = SS_ALLOC_N(requiredBufferSize);

    if (!entry->buf) {
      entry->buf = oldBuffer;
      return -1;
    } else {
      entry->_b_size = requiredBufferSize;
    }

    memcpy(entry->buf, oldBuffer, oldSize);
    SS_FREE(oldBuffer);
  }
  const size_t buffer_left = entry->_b_size - entry->_p;
  const size_t elements_to_copy = buffer_left > size * count ? count : buffer_left / size;

  const uint8_t content_is_different = memcmp(entry->buf + entry->_p, prt, size * elements_to_copy);

  if (content_is_different) {
    memcpy(entry->buf + entry->_p, prt, size * elements_to_copy);
    entry->_b_dirty = 1;
  }
  entry->_p += size * elements_to_copy;

  return elements_to_copy;
}
