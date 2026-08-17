# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only
#
# Wire the real onomondo-uicc core into a native_sim fuzz app, the same way
# tests/apdu/CMakeLists.txt wires it for the transcript suite. Include this file
# after find_package(Zephyr)/project(), then call:
#
#   softsim_fuzz_core(HEAP system|port)
#   target_sources(app PRIVATE ${SOFTSIM_FUZZ_COMMON}/fuzz_entry.c src/main.c ...)
#   target_link_libraries(app PRIVATE uicc storage milenage crypto)
#
# HEAP system -> SS_ALLOC maps to malloc/free, so ASan/LSan see every core
#   allocation (real leak detection on the TLV decoders). Use when no Zephyr heap
#   port (ss_heap.c) is linked.
# HEAP port   -> SS_ALLOC maps to port_malloc (k_malloc). Required when the nRF
#   filesystem port (ss_fs.c/ss_heap.c) is in the build, matching firmware.

# Resolved once at include time: this file lives in tests/fuzz/common, so the
# repo lib/ dir (which holds both the nRF port and the onomondo-uicc submodule)
# is three levels up. Callers get ${SOFTSIM_LIB} and ${SOFTSIM_FUZZ_COMMON}.
get_filename_component(SOFTSIM_LIB "${CMAKE_CURRENT_LIST_DIR}/../../../lib" ABSOLUTE)
set(SOFTSIM_FUZZ_COMMON "${CMAKE_CURRENT_LIST_DIR}")

macro(softsim_fuzz_core)
  cmake_parse_arguments(SF "" "HEAP" "" ${ARGN})

  if(SF_HEAP STREQUAL "system")
    set(_sf_system_heap ON)
  elseif(SF_HEAP STREQUAL "port")
    set(_sf_system_heap OFF)
  else()
    message(FATAL_ERROR "softsim_fuzz_core: HEAP must be 'system' or 'port', got '${SF_HEAP}'")
  endif()

  # The same cache-var switches lib/CMakeLists.txt uses to build the core for
  # firmware, with software crypto instead of PSA (no TF-M off-target). These
  # are plain CMake cache vars consumed by the submodule and must be set before
  # add_subdirectory().
  set(CONFIG_EXTERNAL_CRYPTO_IMPL OFF CACHE BOOL "" FORCE)
  set(CONFIG_USE_SYSTEM_HEAP ${_sf_system_heap} CACHE BOOL "" FORCE)
  set(CONFIG_USE_LOGS OFF CACHE BOOL "" FORCE)
  set(CONFIG_NO_DEFAULT_IMPL ON CACHE BOOL "" FORCE)
  set(CONFIG_COMPACT_STORAGE ON CACHE BOOL "" FORCE)
  set(CONFIG_BUILD_LIB_ONLY ON CACHE BOOL "" FORCE)
  set(CONFIG_USE_EXPERIMENTAL_SUSPEND_COMMAND ON CACHE BOOL "" FORCE)
  add_subdirectory(${SOFTSIM_LIB}/onomondo-uicc onomondo-uicc)

  # Same propagation lib/CMakeLists.txt does for firmware: the subproject targets
  # do not inherit Zephyr's flags on their own, and without them the libraries
  # build for the host ABI (elf64) while native_sim links elf32. The fuzzer/ASan
  # instrumentation flags reach the core the same way (they are on
  # zephyr_interface once --enable-asan / CONFIG_ARCH_POSIX_LIBFUZZER are set).
  foreach(_t uicc storage milenage crypto)
    target_compile_definitions(${_t} PRIVATE
      $<TARGET_PROPERTY:zephyr_interface,INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(${_t} PRIVATE
      $<TARGET_PROPERTY:zephyr_interface,INTERFACE_COMPILE_OPTIONS>)
  endforeach()

  # Private core headers the harnesses reach for (apdu.h, btlv.h, sms.h, ...)
  # plus the public include tree.
  target_include_directories(app PRIVATE
    ${SOFTSIM_LIB}
    ${SOFTSIM_LIB}/include
    ${SOFTSIM_LIB}/onomondo-uicc/include
    ${SOFTSIM_LIB}/onomondo-uicc/src
    ${SOFTSIM_LIB}/onomondo-uicc/src/softsim/uicc)
endmacro()
