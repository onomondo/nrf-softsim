#include <zephyr/zephyr.h>
#include <autoconf.h>

#define EXPECTED_PARTITION_SIZE 0x8000
#define EXPECTED_MIN_HEAP_SIZE 30000

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE >= EXPECTED_MIN_HEAP_SIZE, "SoftSIM: "
             "Heap memory pool size is not valid. "
             "Please reconfigure the project.");

/* In NCS, when NVS backend for Settings is chosen, `nvs_partition` partition is not included by
 * the Partition Manager.
 * `nvs_storage`partition is required by SoftSIM. FCB backend for Settings should be used instead
 * of NVS backend.
 */
#if CONFIG_SETTINGS_NVS
BUILD_ASSERT(0, "Softsim: Please disable CONFIG_SETTINGS_NVS. Choose CONFIG_SETTINGS_FCB instead.");
#else
BUILD_ASSERT(CONFIG_PM_PARTITION_SIZE_NVS_STORAGE == EXPECTED_PARTITION_SIZE, "SoftSIM: "
             "nvs_partition size is not valid. "
             "Please reconfigure the project.");
#endif
