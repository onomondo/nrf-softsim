/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <autoconf.h>

#include <onomondo/utils/ss_profile.h>

#define EXPECTED_PARTITION_SIZE 0x8000
#define EXPECTED_MIN_HEAP_SIZE  30000

/* nrf_softsim_provision() validates the parsed profile by indexing the KI/OPc pair in
 * A001 and the KIC/KID pair in A004. Pin those layouts so a submodule bump that moves
 * them fails the build instead of silently reading the wrong bytes. */
BUILD_ASSERT(2 * KEY_SIZE <= A001_LEN, "SoftSIM: A001 layout changed");
BUILD_ASSERT(A004_HEADER_SIZE + 2 * KEY_SIZE <= A004_LEN, "SoftSIM: A004 layout changed");

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
#elif defined(CONFIG_PARTITION_MANAGER_ENABLED)
BUILD_ASSERT(CONFIG_PM_PARTITION_SIZE_NVS_STORAGE == EXPECTED_PARTITION_SIZE,
	     "SoftSIM: "
	     "nvs_partition size is not valid. "
	     "Please reconfigure the project.");
#else
/* Partition Manager is deprecated from NCS 3.4 and applications may define
 * nvs_storage as a plain devicetree partition instead (see the Asset Tracker
 * Template port). CONFIG_PM_PARTITION_SIZE_NVS_STORAGE does not exist there and
 * would silently evaluate to 0, so measure the partition itself. */
BUILD_ASSERT(FIXED_PARTITION_SIZE(nvs_storage) == EXPECTED_PARTITION_SIZE,
	     "SoftSIM: "
	     "nvs_storage partition size is not valid. "
	     "Please reconfigure the project.");
#endif
