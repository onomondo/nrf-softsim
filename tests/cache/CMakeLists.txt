# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_cache_test)

set(SOFTSIM_LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)

# Compile the real sources rather than a copy. ss_cache.c touches no NVS, no PSA
# and no TF-M, so it is added directly instead of selecting CONFIG_SOFTSIM --
# which `depends on BUILD_WITH_TFM` and therefore cannot be satisfied here.
target_sources(app PRIVATE
  src/main.c
  ${SOFTSIM_LIB}/ss_cache.c
  ${SOFTSIM_LIB}/ss_heap.c
)

target_include_directories(app PRIVATE
  ${SOFTSIM_LIB}
  ${SOFTSIM_LIB}/onomondo-uicc/include
)

# ss_cache.c references exactly two Kconfig symbols, and both live under
# `if SOFTSIM` in the module Kconfig, which is not selected here. Supply the log
# level directly; the heap follows the production path (SS_ALLOC -> port_malloc
# -> k_malloc) because ss_heap.c is compiled in above.
target_compile_definitions(app PRIVATE CONFIG_SOFTSIM_NRF_LOG_LEVEL=3)
