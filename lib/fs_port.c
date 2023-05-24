#include <onomondo/softsim/fs_port.h>
#include <onomondo/softsim/list.h>
#include <onomondo/softsim/utils.h>
#include <onomondo/softsim/log.h>
#include <onomondo/softsim/mem.h>
#include <stdlib.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <psa/storage_common.h>
#include <psa/protected_storage.h>

#include "f_cache.h"
#include "provision.h"

LOG_MODULE_REGISTER(ss_storage, LOG_LEVEL_DBG);

static struct nvs_fs fs;
static struct ss_list fs_cache;

#define NVS_PARTITION storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define DIR_ID (1UL)
#define IMSI_LEN (18)
#define A001_LEN (66)
#define A004_LEN (228)
#define ICCID_LEN (20)
#define PROFILE_LEN (IMSI_LEN + A001_LEN + A004_LEN + ICCID_LEN)

#define IMSI_OFFSET (0)
#define A001_OFFSET (IMSI_OFFSET + IMSI_LEN)
#define A004_OFFSET (A001_OFFSET + A001_LEN)
#define ICCID_OFFSET (A004_OFFSET + A004_LEN)

#define IMSI_PATH "/3f00/7ff0/6f07"
#define ICCID_PATH "/3f00/2fe2"
#define A001_PATH "/3f00/a001"
#define A004_PATH "/3f00/a004"

#ifndef SEEK_SET
#define SEEK_SET 0 /* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1 /* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define SEEK_END 2 /* set file offset to EOF plus offset */
#endif
// internal functions

//  that the buffer is set. Either by allocating new memory or by
// stealing from another entry.
void read_nvs_to_cache(struct cache_entry *entry);

uint8_t fs_is_initialized = 0;

int init_fs() {

  if (fs_is_initialized)
    return 0; // already initialized

  ss_list_init(&fs_cache);
  uint8_t *data = NULL;
  size_t len = 0;

  /*************************
   * Bootstrapping not needed for final version. Content will be flashed with
   *application.
   **************************/
  uint8_t isDemoBootstrapping = 0;

  fs.flash_device = NVS_PARTITION_DEVICE;
  fs.sector_size = 0x1000; // where to read this? :DT_PROP(NVS_PARTITION,
                           // erase_block_size);  //<0x1000>
  fs.sector_count = FLASH_AREA_SIZE(storage_partition) / fs.sector_size;
  fs.offset = NVS_PARTITION_OFFSET;

  int rc = nvs_mount(&fs);
  if (rc) {
    LOG_ERR("failed to mount nvs\n");
    return -1;
  }

  rc = nvs_read(&fs, DIR_ID, NULL, 0);

  /*************************
   * This should never happen in production version.
   * At this point we should panic and return when not generationg this on
   *device
   **************************/
  if (rc <= 0) {

    printk("bootstrapping for demo\n");
    generate_dir_blob(&data, &len); // debug. remove later
    ++isDemoBootstrapping;
    rc = nvs_write(&fs, DIR_ID, data, len);
  } else {
    len = rc;
  }

  /*************************
   * Read DIR_ENTRY from NVS
   * This is used to construct a linked list that
   * serves as a cache and lookup table for the filesystem
   */
  if (!data && rc) {
    data = SS_ALLOC_N(len * sizeof(uint8_t));
    rc = nvs_read(&fs, DIR_ID, data, len);
    assert(rc == len);
  }

  ss_list_init(&fs_cache);
  generate_dir_table_from_blob(&fs_cache, data, len);

  if (ss_list_empty(&fs_cache))
    goto out;

  struct cache_entry *cursor;

  /*************************
   * TODO: goto out;
   * the rest of this function is only for demo purposes
   */
  if (isDemoBootstrapping) { // debug only. "self provisioning" sim
    printk("writing default profile\n");
    SS_LIST_FOR_EACH(&fs_cache, cursor, struct cache_entry, list) {
      // checking if file actually exist:
      rc = nvs_read(&fs, cursor->key, NULL, 0);
      if (rc >= 0)
        continue;

      if (cursor->_flags & FS_PROTECTED_STORAGE ||
          cursor->_flags & FS_READ_ONLY) {
        printk("Skipping %s, key: %d flags: %04X\n", cursor->name, cursor->key,
               cursor->_flags);
        continue;
      }

      // printk("File %s does not exist in NVS, key: %d, flags: %04X\n",
      //  cursor->name, cursor->key, cursor->_flags);
      char *p = getFilePointer(cursor->name);

      if (p) {

        size_t len = strlen(p);
        // printk("writing %s, key: %d, len: %d\n", cursor->name, cursor->key,
        // len);
        rc = nvs_write(&fs, cursor->key, p, len);
      } else {
        printk("File %s does not exist in FS\n", cursor->name);
      }

      if (rc < 0) {
        printk("Failed to write file %s to NVS", cursor->name);
      }
    }
  }
out:
  free(data);
  return ss_list_empty(&fs_cache);
}

/**
 * @brief Implements a version of standard C fopen.
 * @param path Full path.
 * @param mode Currently ignorred.
 * @return Pointer to a
 * "file" represented by a struct cache_entry internally.
 */
port_FILE port_fopen(char *path, char *mode) {
  struct cache_entry *cursor = NULL;
  int rc = 0;

  cursor = f_cache_find_by_name(path, &fs_cache); //
  if (!cursor) {
    return NULL;
  }

  /**
   * Currently not used.
   * Could potentially be used in the future to re-arrange order.
   * Initial order is ordered by frequency already so not big optimizations can
   * be achieved.
   */
  if (cursor->_cache_hits < 0xFF)
    cursor->_cache_hits++;

  /**
   * File opened first time.
   */
  if (!cursor->_l) {

    if (cursor->_flags & FS_PROTECTED_STORAGE) {
      struct psa_storage_info_t info;
      psa_status_t status = psa_ps_get_info(cursor->key, &info);
      if (status != PSA_SUCCESS) {
        LOG_ERR("PSA get info failed\n");
        return NULL;
      }

      rc = info.size;

    } else {
      rc = nvs_read(&fs, cursor->key, NULL, 0);
    }

    if (rc < 0) { // TODO: this can not happen.
      return NULL;
    } else {
      cursor->_l = rc;
    }
  }

  // Reset internal read/write pointer
  cursor->_p = 0;

  // Guarentee buffer contains valid data
  read_nvs_to_cache(cursor);
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
size_t port_fread(void *ptr, size_t size, size_t nmemb, port_FILE fp) {
  if (nmemb == 0 || size == 0) {
    return 0;
  }
  struct cache_entry *entry = (struct cache_entry *)fp;

  size_t max_element_to_return = (entry->_l - entry->_p) / size;
  size_t element_to_return =
      nmemb > max_element_to_return ? max_element_to_return : nmemb;

  memcpy(ptr, entry->buf + entry->_p, element_to_return * size);

  entry->_p += element_to_return * size; // move internal read/write pointer
  return element_to_return;
}

/**
 * @brief Makes sure internal buffer points to the actual data by fetching it
 * from NVS or protected storage if needed.
 * @param entry Pointer to a cache entry.
 */
void read_nvs_to_cache(struct cache_entry *entry) {
  struct cache_entry *tmp;

  /**
   * If entry has a buffer assigned it contains valid data.
   * Return early.
   */
  if (entry->buf)
    return;

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
    } else { // or reused
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
    LOG_DBG("Allocating buffer of size %d\n", buffer_size);
    buffer_to_use = SS_ALLOC_N(buffer_size * sizeof(uint8_t));
  }

  if (!buffer_to_use) {
    LOG_ERR("Failed to allocate buffer of size %d\n", buffer_size);
    return;
  }

  int rc = 0;
  if (entry->_flags & FS_PROTECTED_STORAGE) {
    psa_status_t status =
        psa_ps_get(entry->key, 0, buffer_size, buffer_to_use, &rc);
    if (status != PSA_SUCCESS) {
      LOG_ERR("Failed to read file %s from protected storage", entry->name);
      SS_FREE(buffer_to_use);
      return;
    }
  } else {
    rc = nvs_read(&fs, entry->key, buffer_to_use, buffer_size);
  }

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

  if (entry->_p >= entry->_l) { // no more data
    return NULL;
  }

  int idx = 0; // dest idx

  while (entry->_p < entry->_l && idx < n - 1 &&
         entry->buf[entry->_p] != '\0' && entry->buf[entry->_p] != '\n') {
    str[idx++] = entry->buf[entry->_p++];
  }

  str[idx] = '\0';
  return str;
}

int port_fclose(port_FILE fp) {
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

  if (entry->_flags & FS_SENSITIVE_DATA && entry->buf) {
    ss_memzero(entry->buf, entry->_b_size);
    SS_FREE(entry->buf);
    entry->buf = NULL; // actually validate
    entry->_l = 0;     // force new read
  }

out:
  entry->_p = 0; // TODO: not needed?
  return 0;
}

int port_fseek(port_FILE fp, long offset, int whence) {
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return -1;
  }

  if (whence == SEEK_SET) {
    entry->_p = offset;
  } else if (whence == SEEK_CUR) {
    entry->_p += offset;
    if (entry->_p >= entry->_l)
      entry->_p = entry->_l - 1; // how is std c behaving here?
  } else if (whence == SEEK_END) {
    entry->_p = entry->_l - offset;
  }
  return 0;
}
long port_ftell(port_FILE fp) {
  struct cache_entry *entry = (struct cache_entry *)fp;

  if (!entry) {
    return -1;
  }

  return entry->_p;
}
int port_fputc(int c, port_FILE fp) {
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
int port_access(const char *path, int amode) {
  return 0;
} // TODO -> safe to omit for now. Internally SoftSIM will verify that a
  // directory exists after creation. Easier to guarentee since it isn't a
  // 'thing'
int port_mkdir(const char *, int) {
  return 0;
} // don't care. We don't really obey directories (creating file
  // 'test/a/b/c.def) implicitly creates the directories

int port_remove(const char *path) {
  struct cache_entry *entry = f_cache_find_by_name(path, &fs_cache);

  if (!entry) {
    return -1;
  }

  ss_list_remove(&entry->list); // doesn't free data

  nvs_delete(&fs, entry->key);

  if (entry->buf)
    SS_FREE(entry->buf);

  SS_FREE(entry->name);
  SS_FREE(entry);

  return 0;
}

int port_rmdir(const char *) {
  return 0;
} // todo. Remove all entries with directory match.
// ie remove 7fff/test
// remove 7fff/test/a
// remove 7fff/test/a/b

// list for each { is_name_partial_match? {remove port_remove(cursor->name)} }

/**
 * @brief Provision the SoftSIM with the given profile
 *
 *
 * @param profile ptr to the profile
 * @param len Len of profile. 332 otherwise invalid.
 */
void port_provision(char *profile, size_t len) {
  int rc = init_fs();
  if (rc)
    return;

  if (len != PROFILE_LEN) {
    return;
  }

  // check for existing provisioning
  struct psa_storage_info_t info;
  psa_status_t status;

  // IMSI 2FE2
  struct cache_entry *entry =
      (struct cache_entry *)f_cache_find_by_name(IMSI_PATH, &fs_cache);
  psa_storage_uid_t uid = entry->key;

  status = psa_ps_get_info(uid, &info);
  if (status == PSA_SUCCESS) {
    LOG_INF("SoftSIM already provisioned\n");
    return;
  }

  status =
      psa_ps_set(uid, IMSI_LEN, profile + IMSI_OFFSET, PSA_STORAGE_FLAG_NONE);
  if (status != PSA_SUCCESS) {
    LOG_ERR("Failed to provision IMSI, err: %d\n", status);
    return;
  }

  // // check if we get expected..
  // entry = (struct cache_entry *)port_fopen(IMSI_PATH, "r");
  // char imsi[25] = {0};
  // port_fgets(imsi, 25, entry);
  // printk("IMSI: %s\n", imsi);
  // port_fclose(entry);

  entry = (struct cache_entry *)f_cache_find_by_name(ICCID_PATH, &fs_cache);
  uid = entry->key;
  status =
      psa_ps_set(uid, ICCID_LEN, profile + ICCID_OFFSET, PSA_STORAGE_FLAG_NONE);
  if (status != PSA_SUCCESS) {
    LOG_ERR("Failed to provision ICCID, err: %d\n", status);
    return;
  }

  entry = (struct cache_entry *)f_cache_find_by_name(A001_PATH, &fs_cache);
  uid = entry->key;
  status = psa_ps_set(uid, A001_LEN, profile + A001_OFFSET,
                      PSA_STORAGE_FLAG_WRITE_ONCE);
  if (status != PSA_SUCCESS && status != PSA_ERROR_ALREADY_EXISTS) {
    LOG_ERR("Failed to provision A001, err: %d\n", status);
    return;
  }

  entry = (struct cache_entry *)f_cache_find_by_name(A004_PATH, &fs_cache);
  uid = entry->key;
  status = psa_ps_set(uid, A004_LEN, profile + A004_OFFSET,
                      PSA_STORAGE_FLAG_WRITE_ONCE);
  if (status != PSA_SUCCESS && status != PSA_ERROR_ALREADY_EXISTS) {
    LOG_ERR("Failed to provision A004, err: %d\n", status);
    return;
  }
}

size_t port_fwrite(const void *prt, size_t size, size_t count, port_FILE f) {
  struct cache_entry *entry = (struct cache_entry *)f;

  if (!entry) {
    return -1;
  }

  const size_t requiredBufferSize = entry->_p + size * count;

  /**
   * In reality this rarely occurs. Potentially when upating SIM OTA
   */
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
  const size_t elements_to_copy =
      buffer_left > size * count ? count : buffer_left / size;

  const uint8_t content_is_different =
      memcmp(entry->buf + entry->_p, prt, size * elements_to_copy);

  if (content_is_different) {
    memcpy(entry->buf + entry->_p, prt, size * elements_to_copy);
    entry->_b_dirty = 1;
  }
  entry->_p += size * elements_to_copy;

  return elements_to_copy;
}
