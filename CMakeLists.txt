# SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

add_subdirectory_ifdef(CONFIG_SOFTSIM lib)

if(CONFIG_SOFTSIM AND NOT CONFIG_PARTITION_MANAGER_ENABLED)
  # Devicetree partitioning: generate the template SoftSIM filesystem hex at
  # the nvs_storage partition address. (With the deprecated Partition Manager
  # this is done in sysbuild/CMakeLists.txt from PM's computed address.)
  dt_nodelabel(_softsim_nvs_path NODELABEL nvs_storage)
  if(NOT _softsim_nvs_path)
    message(FATAL_ERROR "SoftSIM: no nvs_storage partition in the devicetree. "
      "Include a SoftSIM partition layout, e.g. "
      "#include <softsim/nrf91_softsim_partitions.dtsi> in your board overlay.")
  endif()
  dt_reg_addr(_softsim_nvs_addr PATH ${_softsim_nvs_path})

  set(_softsim_template_hex ${CMAKE_BINARY_DIR}/onomondo-softsim/template.hex)
  add_custom_command(
    OUTPUT ${_softsim_template_hex}
    COMMAND ${CMAKE_OBJCOPY}
    --input-target=binary
    --output-target=ihex
    --change-address ${_softsim_nvs_addr}
    ${CMAKE_CURRENT_LIST_DIR}/lib/profile/template.bin
    ${_softsim_template_hex}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/lib/profile/template.bin
  )
  add_custom_target(onomondo_softsim_template ALL DEPENDS ${_softsim_template_hex})

  if(CONFIG_SOFTSIM_FLASH_BUNDLED_HEX)
    # Make west flash program the bundled sysbuild artifact (firmware +
    # template) instead of TF-M's tfm_merged.hex, which lacks the template --
    # a fresh device flashed without the template fails SoftSIM FS init.
    # The Partition Manager flow did the equivalent via its own merged.hex.
    # The ../ hop is coupled to sysbuild/CMakeLists.txt writing the artifact
    # to its ${CMAKE_BINARY_DIR}/merged.hex: sysbuild nests each image's
    # build directory one level below its own.
    cmake_path(SET _softsim_merged_hex NORMALIZE ${CMAKE_BINARY_DIR}/../merged.hex)
    set_property(TARGET runners_yaml_props_target PROPERTY hex_file ${_softsim_merged_hex})
  endif()
endif()
