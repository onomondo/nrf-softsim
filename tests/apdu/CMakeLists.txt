# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_apdu_test)

set(SOFTSIM_LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)

# The real onomondo-uicc core, built the same way lib/CMakeLists.txt builds it
# for firmware -- same cache-var switches -- with one exception: the software
# crypto implementation instead of the PSA one, since there is no TF-M here.
# The variables are plain CMake cache vars consumed by the submodule, and must
# be set before add_subdirectory().
set(CONFIG_EXTERNAL_CRYPTO_IMPL OFF CACHE BOOL "" FORCE)
set(CONFIG_USE_SYSTEM_HEAP OFF CACHE BOOL "" FORCE)
set(CONFIG_USE_LOGS ON CACHE BOOL "" FORCE)
set(CONFIG_NO_DEFAULT_IMPL ON CACHE BOOL "" FORCE)
set(CONFIG_COMPACT_STORAGE ON CACHE BOOL "" FORCE)
set(CONFIG_BUILD_LIB_ONLY ON CACHE BOOL "" FORCE)
set(CONFIG_USE_EXPERIMENTAL_SUSPEND_COMMAND ON CACHE BOOL "" FORCE)
add_subdirectory(${SOFTSIM_LIB}/onomondo-uicc onomondo-uicc)

# Same propagation lib/CMakeLists.txt does for firmware: the subproject
# targets do not inherit Zephyr's flags on their own, and without them the
# libraries build for the host ABI (elf64) while native_sim links elf32.
foreach(_t uicc storage milenage crypto)
  target_compile_definitions(${_t} PRIVATE
    $<TARGET_PROPERTY:zephyr_interface,INTERFACE_COMPILE_DEFINITIONS>)
  target_compile_options(${_t} PRIVATE
    $<TARGET_PROPERTY:zephyr_interface,INTERFACE_COMPILE_OPTIONS>)
endforeach()

# Same port sizing lib/CMakeLists.txt applies for firmware (Kconfig defaults),
# so the suite exercises the core as shipped.
target_compile_definitions(uicc PRIVATE
  SS_FS_CHG_PATH_MAXLEN=10
  SS_FS_CHG_MAX_FILES=20
  SS_SMS_RX_MAX_PARTS=5
)

# The nRF port of the UICC filesystem underneath the core, on the real Zephyr
# NVS over the flash simulator -- the integration this suite exists to pin.
target_sources(app PRIVATE
  src/main.c
  ${SOFTSIM_LIB}/ss_fs.c
  ${SOFTSIM_LIB}/ss_cache.c
  ${SOFTSIM_LIB}/ss_heap.c
  ${SOFTSIM_LIB}/ss_logp_zephyr.c
)

target_link_libraries(app PRIVATE uicc storage milenage crypto)

target_include_directories(app PRIVATE
  ${SOFTSIM_LIB}
  ${SOFTSIM_LIB}/include
  ${SOFTSIM_LIB}/onomondo-uicc/include
)

# No CONFIG_SOFTSIM_LIBS_LOG_LEVEL: the log-level symbols sit at the top
# level of the module Kconfig, so autoconf.h already defines it in any
# workspace build and a -D with a different value is a redefinition error.
# ss_logp_zephyr.c picks it up from autoconf. (CONFIG_SOFTSIM_NRF_LOG_LEVEL=3
# matches autoconf's value byte for byte, so it stays for parity with the
# other suites.)
target_compile_definitions(app PRIVATE
  CONFIG_SOFTSIM_NRF_LOG_LEVEL=3
  CONFIG_COMPACT_STORAGE=1
)

# The pre-populated filesystem image the firmware flashes at the nvs_storage
# partition base; the suite writes it to the simulated flash instead.
generate_inc_file_for_target(app ${SOFTSIM_LIB}/profile/template.bin
  ${ZEPHYR_BINARY_DIR}/include/generated/template_bin.inc)

# Same off-target exemptions as tests/fs/CMakeLists.txt, confined to ss_fs.c;
# see the comment there.
set_source_files_properties(${SOFTSIM_LIB}/ss_fs.c
  DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  PROPERTIES COMPILE_OPTIONS "-Wno-error;-DFLASH_AREA_SIZE(label)=PARTITION_SIZE(label)"
)
