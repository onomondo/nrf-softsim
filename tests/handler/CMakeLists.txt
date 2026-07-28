# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_handler_test)

set(SOFTSIM_LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)

# nrf_softsim.c is the unit under test. Everything it calls below itself -- the
# UICC core, the filesystem port and the modem SoftSIM API -- is faked in
# src/main.c, so the real kernel work queue and FIFO still run. The one
# exception is the profile parser: the provisioning tests drive the REAL
# ss_profile_from_string (it is self-contained -- string.h only), because what
# they pin is nrf_softsim_provision()'s validation of the parser's output.
target_sources(app PRIVATE
  src/main.c
  ${SOFTSIM_LIB}/nrf_softsim.c
  ${SOFTSIM_LIB}/onomondo-uicc/utils/ss_profile.c
)

target_include_directories(app PRIVATE
  ${SOFTSIM_LIB}
  ${SOFTSIM_LIB}/include
  ${SOFTSIM_LIB}/onomondo-uicc/include
  ${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_modem/include
  ${ZEPHYR_NRF_MODULE_DIR}/include
)

# CONFIG_SOFTSIM_AUTO_INIT is deliberately left undefined so the SYS_INIT hook
# is compiled out and the test drives onomondo_init() explicitly.
target_compile_definitions(app PRIVATE CONFIG_SOFTSIM_NRF_LOG_LEVEL=3)
