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
