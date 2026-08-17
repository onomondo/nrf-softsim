# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_fuzz_parse_apdu)

include(${CMAKE_CURRENT_LIST_DIR}/../common/softsim_fuzz.cmake)

# system heap: SS_ALLOC -> malloc, so LSan sees leaks in the TLV decoders'
# linked lists (a decoder that leaks on malformed input is a real DoS vector).
softsim_fuzz_core(HEAP system)

# ss_profile_from_string lives in utils/, is self-contained (string.h only), and
# is compiled straight in the way tests/handler does -- no need for the utils lib.
target_sources(app PRIVATE
  ${SOFTSIM_FUZZ_COMMON}/fuzz_entry.c
  src/main.c
  ${SOFTSIM_LIB}/onomondo-uicc/utils/ss_profile.c
)

# The stateless parsers all live in libuicc and reach neither storage, crypto
# nor the filesystem (verified: no ss_storage_*/ss_fs_*/ss_crypto_* references),
# so link only that -- no fs backend needed, no undefined port hooks.
target_link_libraries(app PRIVATE uicc)
