#include <onomondo/softsim/fs_port.h>
#include <onomondo/softsim/utils.h>
#include <onomondo/softsim/log.h>
#include <onomondo/softsim/mem.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include "provision.h"
#include "profile.h"

LOG_MODULE_DECLARE(softsim, CONFIG_SOFTSIM_LOG_LEVEL);


#define NVS_PARTITION nvs_storage
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)

#define DIR_ID (1UL)

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


static uint8_t fs_is_initialized = 0;

int init_fs() {
  if (fs_is_initialized) return 0;  // already initialized

  // fs.flash_device = NVS_PARTITION_DEVICE;
  // fs.sector_size = 0x1000;  // where to read this? :DT_PROP(NVS_PARTITION,  erase_block_size);  //<0x1000>
  // fs.sector_count = FLASH_AREA_SIZE(nvs_storage) / fs.sector_size;
  // fs.offset = NVS_PARTITION_OFFSET;

  fs_is_initialized++;
  return 0;
}

/*
 * This will only be called when softsim is deinitialized.
 * I.e. when the modem goes to cfun=0 or cfun=4
 * */
int deinit_fs() {
  // maybe you want to unmount the partition here?

  fs_is_initialized = 0;
  return 0;
}

/**
 * @brief Implements a version of standard C fopen.
 * @param path full path.
 * @param mode 
 * @return pointer to a file handle
 */
port_FILE port_fopen(char *path, char *mode) {
  int rc = 0;
  return NULL;
}

/**
 * @brief Implements a version of standard C fread. 
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
  size_t elements_read = 0;
  return elements_read;
}


char *port_fgets(char *str, int n, port_FILE fp) {
  // OK to just return all data in a file.
  //
  // Debug build uses an internal hex to bytes conversion.

  size_t bytes_read = port_fread(str, 1, n, fp);

  if (bytes_read == 0) {
    return NULL;
  }

  return str;
}

int port_fclose(port_FILE fp) {
}

int port_fseek(port_FILE fp, long offset, int whence) {
  // if (whence == SEEK_SET) {
  //   entry->_p = offset;
  // } else if (whence == SEEK_CUR) {
  //   entry->_p += offset;
  //   if (entry->_p >= entry->_l) entry->_p = entry->_l - 1;  // how is std c behaving here?
  // } else if (whence == SEEK_END) {
  //   entry->_p = entry->_l - offset;
  // }
  return 0;
}
long port_ftell(port_FILE fp) {
  // struct cache_entry *entry = (struct cache_entry *)fp;
  //
  // if (!entry) {
  //   return -1;
  // }
  //
  // return entry->_p;
}
int port_fputc(int c, port_FILE fp) {
  // size_t written = port_fwrite(&c, 1, 1, fp); 
  //
  // if (written == 1) {
  //   return c;
  // }
  // return -1;
}


// TODO -> safe to omit for now. Internally SoftSIM will verify that a
// directory exists after creation.
int port_access(const char *path, int amode) {
  return 0;
}

int port_mkdir(const char *, int) {
  return 0;
}  

int port_remove(const char *path) {
  // nvs_delete(&fs, entry->key);
  return 0;
}

// very unlike to be invoked tbh
int port_rmdir(const char *) { return 0; }  // todo. Remove all entries with directory match.

static uint8_t default_imsi[] = {0x08, 0x09, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x10};

int port_check_provisioned() {

  // read data at IMSI_PATH

  // IMSI read is still the default one from the template
  if (memcmp(buffer, default_imsi, IMSI_LEN) == 0) {
    return 0;
  }

  return 1;
}

/**
 * @brief Provision the SoftSIM with the given profile
 *
 *
 * @param profile ptr to the profile
 * @param len Len of profile. 332 otherwise invalid.
 */
int port_provision(struct ss_profile *profile) {
  int rc = init_fs();
  if (rc) {
    LOG_ERR("Failed to init FS");
  }

  // // IMSI 6f07
  // struct cache_entry *entry = (struct cache_entry *)f_cache_find_by_name(IMSI_PATH, &fs_cache);
  //
  // LOG_INF("Provisioning SoftSIM 1/4");
  // if (nvs_write(&fs, entry->key, profile->IMSI, IMSI_LEN) < 0) goto out_err;
  // entry->_flags = 0;
  //
  // LOG_INF("Provisioning SoftSIM 2/4");
  // entry = (struct cache_entry *)f_cache_find_by_name(ICCID_PATH, &fs_cache);
  // if (nvs_write(&fs, entry->key, profile->ICCID, ICCID_LEN) < 0) {
  //   goto out_err;
  // }
  // entry->_flags = 0;
  //
  // LOG_INF("Provisioning SoftSIM 3/4");
  // entry = (struct cache_entry *)f_cache_find_by_name(A001_PATH, &fs_cache);
  // if (nvs_write(&fs, entry->key, profile->A001, sizeof(profile->A001)) < 0) goto out_err;
  // entry->_flags = 0;
  //
  // LOG_INF("Provisioning SoftSIM 4/4");
  // entry = (struct cache_entry *)f_cache_find_by_name(A004_PATH, &fs_cache);
  // if (nvs_write(&fs, entry->key, profile->A004, sizeof(profile->A004)) < 0) goto out_err;
  // entry->_flags = 0;
  //
  //
  LOG_INF("SoftSIM provisioned");
  return 0;

out_err:
  LOG_ERR("SoftSIM provisioning failed");
  return -1;
}

size_t port_fwrite(const void *prt, size_t size, size_t count, port_FILE f) {
  size_t elements_written = 0;
  return elements_written;
}
