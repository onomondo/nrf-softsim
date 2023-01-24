#include "fs_port.h"

#include <logging/log.h>
#include <storage/flash_map.h>

LOG_MODULE_REGISTER(ss_storage, LOG_LEVEL_DBG);

// make sure flash and file system is ready to use
int nrf_configure_fs(void);
void print_fs_info(void);

#define PARTITION_NODE DT_NODELABEL(lfs1)
#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else /* PARTITION_NODE */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);

#pragma message "Using default LFS configuration"

// main mount structure
static struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FLASH_AREA_ID(storage),
    .mnt_point = "/lfs",
};
#endif /* PARTITION_NODE */

struct fs_mount_t *mp =
#if DT_NODE_EXISTS(PARTITION_NODE)
    &FS_FSTAB_ENTRY(PARTITION_NODE)
#else
    &lfs_storage_mnt
#endif
    ;

// fs_file and dir containers
static struct fs_statvfs statsvfs;
static uint8_t isInitialized = 0;
struct fs_file_t _file;

nrf_FILE *nrf_fopen(const char *path, const char *mode) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return NULL;
    }

    fs_file_t_init(&_file);
    rc = fs_open(&_file, path, FS_O_RDWR);
    if (rc < 0) {
        return NULL;
    } else {
        return &_file;
    }
}
int nrf_mkdir(const char *path, int mode) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }

    printk("uninplemented: nrf_mkdir\n");

    return -1;
}
int nrf_fclose(nrf_FILE *f) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }

    return fs_close(f);
}

char *nrf_fgets(char *str, int n, nrf_FILE *f) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return NULL;
    }

    int len = fs_read(f, str, n);

    if (len < 0) {
        printk("failed to read file with error: %d\n", len);
        return NULL;
    }

    int line_end = 0;
    for (int i = 0; i < len; i++) {
        if (str[i] == '\n') {
            str[i] = '\0';
            line_end = i;
            break;
        }

        // make sure we terminate the string
        if (i == len - 1) {
            str[len] = '\0';
        }
    }

    // move file cursor back to the beginning of the next line
    if (len > line_end) {
        fs_seek(f, line_end - len, FS_SEEK_CUR);
    }

    if (len > 0) {
        return str;
    }

    return NULL;
}

int nrf_fseek(nrf_FILE *f, long int offset, int whence) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }

    return fs_seek(f, offset, whence);
}

size_t nrf_fread(void *ptr, size_t size, size_t nbmemb, nrf_FILE *f) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }

    int len = fs_read(f, ptr, size * nbmemb);

    if (len <= 0) {
        printk("%s: failed to read file\n", __func__);
        return -1;
    }

    return len / size;
}

// Get current position in stream
long int nrf_ftell(nrf_FILE *f) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }

    return fs_tell(f);
}

// Writes a character to the stream and advances the position indicator.
int nrf_fputc(int character, nrf_FILE *f) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }
    printk("uninplemented: nrf_fputc\n");

    return -1;
}
int nrf_deinit() {
    printk("nrf_deinit\n");
    int rc;
    rc = fs_unmount(mp);
    printk("%s unmount: %d\n", mp->mnt_point, rc);
    isInitialized = 0;
    return rc;
}

size_t nrf_fwrite(const void *prt, size_t size, size_t count, nrf_FILE *f) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }
    size_t len = fs_write(f, prt, size * count);

    return len / size;
}

int nrf_access(const char *host_path, int accessType) {
    int rc = nrf_configure_fs();
    if (rc != 0) {
        printk("%s: failed to configure FS\n", __func__);
        return -1;
    }

    printk("uninplemented: nrf_access\n");

    return -1;
}

int nrf_configure_fs(void) {
    if (isInitialized) {
        return 0;
    }
    int rc;

    rc = fs_mount(mp);
    if (rc < 0) {
        printk("FAIL: mount id %" PRIuPTR " at %s: %d\n",
               (uintptr_t)mp->storage_dev, mp->mnt_point, rc);
        goto out;
    }
    isInitialized = 1;

    return 0;

out:
    return -1;
}

void print_fs_info(void) {
    // return;

    int rc;
    unsigned int id = (uintptr_t)mp->storage_dev;
    const struct flash_area *pfa;

    rc = fs_statvfs(mp->mnt_point, &statsvfs);

    if (rc < 0) {
        printk("FAIL: statvfs: %d\n", rc);
        return;
    }

    printk(
        "%s: bsize = %lu ; frsize = %lu ;"
        " blocks = %lu ; bfree = %lu\n",
        mp->mnt_point, statsvfs.f_bsize, statsvfs.f_frsize, statsvfs.f_blocks,
        statsvfs.f_bfree);

    rc = flash_area_open(id, &pfa);
    if (rc < 0) {
        printk("FAIL: unable to find flash area %u: %d\n", id, rc);
        return;
    }

    printk("Area %u at 0x%x on %s for %u bytes\n", id,
           (unsigned int)pfa->fa_off, pfa->fa_dev_name,
           (unsigned int)pfa->fa_size);

    flash_area_close(pfa);

    struct fs_dir_t dir;

    fs_dir_t_init(&dir);

    rc = fs_opendir(&dir, "/lfs/3f00/");
    printk("%s opendir: %d\n", mp->mnt_point, rc);

    while (rc >= 0) {
        struct fs_dirent ent = {0};

        rc = fs_readdir(&dir, &ent);
        if (rc < 0) {
            break;
        }
        if (ent.name[0] == 0) {
            printk("End of files\n");
            break;
        }
        printk("  %c %zu %s\n", (ent.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
               ent.size, ent.name);
    }
}

int nrf_remove(const char *path) {
    printk("unimplemented: remove\n");
    return -1;
}