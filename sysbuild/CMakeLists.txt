# SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

# This file is a Zephyr module sysbuild hook: it is evaluated for *every*
# sysbuild build in a workspace whose manifest includes this repo, not just for
# SoftSIM applications. Everything below needs the Partition Manager to compute
# the nvs_storage address, and PM is deprecated and no longer enabled by default
# from NCS 3.4 -- so it must all stay behind the SB_CONFIG_PARTITION_MANAGER
# guard. Without it, referencing the (nonexistent) partition_manager target
# fails the configure step of unrelated applications.
if(SB_CONFIG_PARTITION_MANAGER)
  if(CMAKE_OBJCOPY)
    set(ONOMONDO_SOFTSIM_OBJCOPY ${CMAKE_OBJCOPY})
  else()
    find_program(ONOMONDO_SOFTSIM_OBJCOPY
      NAMES arm-zephyr-eabi-objcopy arm-none-eabi-objcopy objcopy
      REQUIRED
    )
  endif()

  set(ONOMONDO_SOFTSIM_BASE_ADDRESS $<TARGET_PROPERTY:partition_manager,PM_NVS_STORAGE_ADDRESS>)
  set(ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT ${CMAKE_BINARY_DIR}/onomondo-softsim/template.hex)

  add_custom_command(
    OUTPUT template.hex
    COMMAND ${ONOMONDO_SOFTSIM_OBJCOPY}
    --input-target=binary
    --output-target=ihex
    --change-address ${ONOMONDO_SOFTSIM_BASE_ADDRESS}
    ${CMAKE_CURRENT_LIST_DIR}/../lib/profile/template.bin
    ${ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT}
    BYPRODUCTS ${ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT} # This is needed for Ninja
  )
  add_custom_target(onomondo_softsim_template DEPENDS template.hex)

  if(SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX)
    set_property(
      GLOBAL PROPERTY
      nvs_storage_PM_HEX_FILE
      ${ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT}
    )

    set_property(
      GLOBAL PROPERTY
      nvs_storage_PM_TARGET
      onomondo_softsim_template
    )
  endif()
elseif(SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX)
  message(FATAL_ERROR
    "SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX requires SB_CONFIG_PARTITION_MANAGER=y: "
    "the template profile is placed at the address the Partition Manager assigns to "
    "the nvs_storage partition. Add SB_CONFIG_PARTITION_MANAGER=y to sysbuild.conf, "
    "or drop SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX and flash the template separately."
  )
endif()

function(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
  if(NOT SB_CONFIG_PARTITION_MANAGER)
    return()
  endif()

  # Set the default NCS static partition layout for the Thingy:91
  if(SB_CONFIG_THINGY91_STATIC_PARTITIONS_FACTORY)
    set(PM_STATIC_YML_FILE ${SYSBUILD_SOFTSIM_MODULE_DIR}/boards/thingy91_pm_static.yml CACHE INTERNAL "")
  endif()

  # Set the default NCS static partition layout for the Thingy:91 X
  if(SB_CONFIG_BOARD_THINGY91X_NRF9151_NS AND SB_CONFIG_THINGY91X_STATIC_PARTITIONS_FACTORY)
    set(PM_STATIC_YML_FILE ${SYSBUILD_SOFTSIM_MODULE_DIR}/boards/thingy91x_nrf9151_pm_static.yml CACHE INTERNAL "")
  endif()
endfunction(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
