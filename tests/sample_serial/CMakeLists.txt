# SPDX-FileCopyrightText: Copyright (c) 2026 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(softsim_sample_serial_test)

set(SOFTSIM_SAMPLE ${CMAKE_CURRENT_LIST_DIR}/../../samples/softsim_external_profile)

# The unit under test is the sample's serial profile receiver, compiled as-is.
# It lives in its own file exactly so it can be linked here -- the sample's
# main.c cannot: its main() collides with ztest's and its modem headers do not
# resolve off-target.
target_sources(app PRIVATE
  src/main.c
  ${SOFTSIM_SAMPLE}/src/profile_serial.c
)

target_include_directories(app PRIVATE ${SOFTSIM_SAMPLE}/src)
