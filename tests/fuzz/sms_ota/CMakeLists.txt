# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_fuzz_sms_ota)

include(${CMAKE_CURRENT_LIST_DIR}/../common/softsim_fuzz.cmake)

# port heap: same as transact -- the nRF filesystem port is in the build, and
# ss_uicc_sms_rx -> ss_uicc_remote_cmd_receive reads keys/counters from storage.
softsim_fuzz_core(HEAP port)

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

generate_inc_file_for_target(app ${SOFTSIM_LIB}/profile/template.bin
  ${ZEPHYR_BINARY_DIR}/include/generated/template_bin.inc)

set_source_files_properties(${SOFTSIM_LIB}/ss_fs.c
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  PROPERTIES COMPILE_OPTIONS "-Wno-error;-DFLASH_AREA_SIZE(label)=PARTITION_SIZE(label)")
