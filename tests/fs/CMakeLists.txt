# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_fs_test)

set(SOFTSIM_LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)

# The filesystem port runs against the real Zephyr NVS on the flash simulator.
# Mocking the storage backend here would be worse than useless: it is exactly
# the sector rotation and cache write-back that the historical bugs lived in.
# ss_fs.c calls no PSA, so no TF-M or secure storage is involved.
target_sources(app PRIVATE
  src/main.c
  ${SOFTSIM_LIB}/ss_fs.c
  ${SOFTSIM_LIB}/ss_cache.c
  ${SOFTSIM_LIB}/ss_heap.c
)

target_include_directories(app PRIVATE
  ${SOFTSIM_LIB}
  ${SOFTSIM_LIB}/include
  ${SOFTSIM_LIB}/onomondo-uicc/include
)

# CONFIG_COMPACT_STORAGE matches what the nRF targets ship.
target_compile_definitions(app PRIVATE
  CONFIG_SOFTSIM_NRF_LOG_LEVEL=3
  CONFIG_COMPACT_STORAGE=1
)

# ss_fs.c carries three dated dependencies that the firmware build tolerates
# because it does not use -Werror, but a test build does:
#
#  - FLASH_AREA_SIZE() is not a Zephyr macro at all. It comes from NCS's
#    Partition Manager shim (nrf/include/flash_map_pm.h), which is absent
#    off-target; there it is a plain alias for PARTITION_SIZE().
#  - <zephyr/fs/nvs.h> is deprecated in favour of <zephyr/kvss/nvs.h>.
#  - FIXED_PARTITION_*() are deprecated in favour of PARTITION_*().
#
# Both deprecations are raised as unclassified warnings, so -Wno-error=<kind>
# cannot reach them. Confine the exemption to that one file rather than relaxing
# it for the whole target, and keep the test sources strict.
set_source_files_properties(${SOFTSIM_LIB}/ss_fs.c
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  PROPERTIES COMPILE_OPTIONS "-Wno-error;-DFLASH_AREA_SIZE(label)=PARTITION_SIZE(label)"
)
