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

# The log-level symbols sit at the top level of the module Kconfig, not under
# `if SOFTSIM`, so autoconf.h already defines them in any workspace build. This
# -D only survives because 3 is exactly the value autoconf derives from
# LOG_DEFAULT_LEVEL; a different value would be a macro-redefinition error.
# The heap follows the production path (SS_ALLOC -> port_malloc -> SoftSIM's
# own k_heap) because ss_heap.c is compiled in above.
target_compile_definitions(app PRIVATE CONFIG_SOFTSIM_NRF_LOG_LEVEL=3)
