/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <autoconf.h>

#define EXPECTED_PARTITION_SIZE 0x8000
#define EXPECTED_MIN_HEAP_SIZE  30000

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE >= EXPECTED_MIN_HEAP_SIZE,
	     "SoftSIM: "
	     "Heap memory pool size is not valid. "
	     "Please reconfigure the project.");

/* In NCS, when NVS backend for Settings is chosen, `nvs_partition` partition is not included by
 * the Partition Manager.
 * `nvs_storage` partition is required by SoftSIM. FCB backend for Settings should be used instead
 * of NVS backend.
 */
#if CONFIG_SETTINGS_NVS
BUILD_ASSERT(0, "SoftSIM: Please disable CONFIG_SETTINGS_NVS. Choose CONFIG_SETTINGS_FCB instead.");
#elif CONFIG_PARTITION_MANAGER_ENABLED
BUILD_ASSERT(CONFIG_PM_PARTITION_SIZE_NVS_STORAGE == EXPECTED_PARTITION_SIZE,
	     "SoftSIM: "
	     "nvs_partition size is not valid. "
	     "Please reconfigure the project.");
#else
/* Devicetree partitioning: the nvs_storage partition must be declared, e.g. by
 * including <softsim/nrf91_softsim_partitions.dtsi> from a board overlay.
 */
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(nvs_storage)),
	     "SoftSIM: "
	     "No nvs_storage partition in the devicetree. "
	     "Include a SoftSIM partition layout from your board overlay.");
#if DT_NODE_EXISTS(DT_NODELABEL(nvs_storage))
BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(nvs_storage)) == EXPECTED_PARTITION_SIZE,
	     "SoftSIM: "
	     "nvs_storage partition size is not valid. "
	     "Please reconfigure the project.");
#endif
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
/* Catches enabling MCUboot on the DKs without switching the application to
 * the MCUboot partition layout (the no-bootloader layout has no
 * boot_partition). */
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(boot_partition)),
	     "SoftSIM: "
	     "MCUboot is enabled but the devicetree has no boot_partition. "
	     "Build with -DEXTRA_DTC_OVERLAY_FILE=mcuboot-partitions.overlay (see README).");
#endif
/* In the SoftSIM layouts the storage_partition label sits on the nvs_storage
 * node (TF-M derives its non-secure SPU flash region from that label), so a
 * settings backend defaulting to storage_partition would write into the
 * SoftSIM filesystem. */
#if (defined(CONFIG_SETTINGS_FCB) || defined(CONFIG_SETTINGS_ZMS)) && \
	DT_NODE_EXISTS(DT_NODELABEL(storage_partition)) && DT_NODE_EXISTS(DT_NODELABEL(nvs_storage))
BUILD_ASSERT(!DT_SAME_NODE(DT_NODELABEL(storage_partition), DT_NODELABEL(nvs_storage)),
	     "SoftSIM: "
	     "The settings storage_partition is the SoftSIM nvs_storage partition. "
	     "Define a dedicated settings partition in your board overlay.");
#endif
#endif
