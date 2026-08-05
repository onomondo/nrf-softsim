# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_crypto_test)

set(SOFTSIM_LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)

# ss_crypto.c talks to PSA and nothing else -- no KMU register access, no
# nrf-specific headers -- so the same file the firmware compiles runs here
# against Mbed TLS's PSA implementation. The "KMU slots" are plain persistent
# PSA key ids (10, 11, 12), which is what makes this testable off-target.
target_sources(app PRIVATE
  src/main.c
  src/ram_its_store.c
  ${SOFTSIM_LIB}/ss_crypto.c
  ${SOFTSIM_LIB}/ss_heap.c
  ${SOFTSIM_LIB}/ss_logp_zephyr.c
)

target_include_directories(app PRIVATE
  ${SOFTSIM_LIB}
  ${SOFTSIM_LIB}/include
  ${SOFTSIM_LIB}/onomondo-uicc/include
)

# No CONFIG_SOFTSIM_LIBS_LOG_LEVEL: the log-level symbols sit at the top level
# of the module Kconfig, so autoconf.h already defines both, and a -D with a
# different value is a redefinition error. 3 is what autoconf derives, so this
# one matches byte for byte and stays for parity with the other suites.
target_compile_definitions(app PRIVATE CONFIG_SOFTSIM_NRF_LOG_LEVEL=3)

# secure_storage_its_store_*() are declared in an internal header that the
# subsystem does not export to applications.
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/subsys/secure_storage/include/internal
)
