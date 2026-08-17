# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_fuzz_transact)

include(${CMAKE_CURRENT_LIST_DIR}/../common/softsim_fuzz.cmake)

# port heap (k_malloc): the nRF filesystem port (ss_fs.c/ss_heap.c) is in the
# build, matching firmware; SS_ALLOC must map to the same heap it does there.
softsim_fuzz_core(HEAP port)

# The nRF port of the UICC filesystem, over Zephyr NVS on the flash simulator --
# the same layering tests/apdu pins, so the fuzzer drives the real storage path
# (DIR cache, sector rotation, path resolution) and not just the parser.
target_sources(app PRIVATE
  ${SOFTSIM_FUZZ_COMMON}/fuzz_entry.c
  src/main.c
  ${SOFTSIM_LIB}/ss_fs.c
  ${SOFTSIM_LIB}/ss_cache.c
  ${SOFTSIM_LIB}/ss_heap.c
)

target_link_libraries(app PRIVATE uicc storage milenage crypto)

target_compile_definitions(app PRIVATE
  CONFIG_SOFTSIM_NRF_LOG_LEVEL=3
  CONFIG_COMPACT_STORAGE=1
)

# The pre-populated filesystem image the firmware flashes at the nvs_storage
# partition base; fuzz_setup() writes it into the simulated flash.
generate_inc_file_for_target(app ${SOFTSIM_LIB}/profile/template.bin
  ${ZEPHYR_BINARY_DIR}/include/generated/template_bin.inc)

# Same off-target exemption tests/apdu/CMakeLists.txt applies to ss_fs.c: the
# Partition Manager shim (nrf/include/flash_map_pm.h, which defines
# FLASH_AREA_SIZE) is absent off-target, so map it to the devicetree size.
set_source_files_properties(${SOFTSIM_LIB}/ss_fs.c
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  PROPERTIES COMPILE_OPTIONS "-Wno-error;-DFLASH_AREA_SIZE(label)=PARTITION_SIZE(label)")
