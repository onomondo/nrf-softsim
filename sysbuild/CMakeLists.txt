# SPDX-FileCopyrightText: Copyright (c) 2023 Onomondo ApS
# SPDX-License-Identifier: GPL-3.0-only

if(SB_CONFIG_PARTITION_MANAGER)
  # Deprecated Partition Manager flow: PM computes the nvs_storage address in
  # sysbuild scope, so the template hex is generated and merged here.
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
  # Devicetree partitioning: the application image generates template.hex at
  # the devicetree nvs_storage address (see the module's CMakeLists.txt).
  # Merge it with the firmware into build/merged.hex -- the same artifact
  # name and location the Partition Manager flow produced.
  get_target_property(_softsim_app_dir ${DEFAULT_IMAGE} _EP_BINARY_DIR)

  # Have west flash program the bundled artifact (firmware + template); the
  # default TF-M runner hex lacks the template and a freshly erased device
  # then fails SoftSIM FS init (see the module's CMakeLists.txt).
  set(${DEFAULT_IMAGE}_CONFIG_SOFTSIM_FLASH_BUNDLED_HEX y CACHE INTERNAL "")

  set(_softsim_merge_hexes ${_softsim_app_dir}/onomondo-softsim/template.hex)
  set(_softsim_merge_deps ${DEFAULT_IMAGE})
  if(SB_CONFIG_SECURE_BOOT_APPCORE)
    # nRF Secure Boot chain (e.g. Thingy:91 X): B0 + provision data +
    # B0-signed MCUboot (S0/S1) + the signed application image.
    get_target_property(_softsim_b0_dir b0 _EP_BINARY_DIR)
    list(APPEND _softsim_merge_hexes
      ${_softsim_b0_dir}/zephyr/zephyr.hex
      ${CMAKE_BINARY_DIR}/app_provision.hex
      ${CMAKE_BINARY_DIR}/signed_by_b0_mcuboot.hex
      ${_softsim_app_dir}/zephyr/zephyr.signed.hex
    )
    list(APPEND _softsim_merge_deps b0 app_provision mcuboot
      ${CMAKE_BINARY_DIR}/app_provision.hex
      ${CMAKE_BINARY_DIR}/signed_by_b0_mcuboot.hex
    )
    if(SB_CONFIG_SECURE_BOOT_BUILD_S1_VARIANT_IMAGE)
      list(APPEND _softsim_merge_hexes ${CMAKE_BINARY_DIR}/signed_by_b0_mcuboot_s1_variant.hex)
      list(APPEND _softsim_merge_deps mcuboot_s1_variant ${CMAKE_BINARY_DIR}/signed_by_b0_mcuboot_s1_variant.hex)
    endif()
  elseif(SB_CONFIG_BOOTLOADER_MCUBOOT)
    # Bootloader plus the signed application image (TF-M + app, slot 0).
    get_target_property(_softsim_mcuboot_dir mcuboot _EP_BINARY_DIR)
    list(APPEND _softsim_merge_hexes
      ${_softsim_mcuboot_dir}/zephyr/zephyr.hex
      ${_softsim_app_dir}/zephyr/zephyr.signed.hex
    )
    list(APPEND _softsim_merge_deps mcuboot)
  else()
    list(APPEND _softsim_merge_hexes ${_softsim_app_dir}/zephyr/tfm_merged.hex)
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/merged.hex
    COMMAND ${PYTHON_EXECUTABLE}
    ${ZEPHYR_BASE}/scripts/build/mergehex.py
    -o ${CMAKE_BINARY_DIR}/merged.hex
    ${_softsim_merge_hexes}
    DEPENDS ${_softsim_merge_deps}
  )
  add_custom_target(softsim_merged_hex ALL DEPENDS ${CMAKE_BINARY_DIR}/merged.hex)
else()
  # Clear a value cached by an earlier configure with the template bundled;
  # otherwise west flash keeps pointing at a merged.hex that is no longer
  # regenerated.
  set(${DEFAULT_IMAGE}_CONFIG_SOFTSIM_FLASH_BUNDLED_HEX n CACHE INTERNAL "")
endif()

function(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
  cmake_parse_arguments(PRE_CMAKE "" "" "IMAGES" ${ARGN})

  if(SB_CONFIG_PARTITION_MANAGER)
    # Set the default NCS static partition layout for the Thingy:91
    if(SB_CONFIG_THINGY91_STATIC_PARTITIONS_FACTORY)
      set(PM_STATIC_YML_FILE ${SYSBUILD_SOFTSIM_MODULE_DIR}/boards/thingy91_pm_static.yml CACHE INTERNAL "")
    endif()

    # Set the default NCS static partition layout for the Thingy:91 X
    if(SB_CONFIG_BOARD_THINGY91X_NRF9151_NS AND SB_CONFIG_THINGY91X_STATIC_PARTITIONS_FACTORY)
      set(PM_STATIC_YML_FILE ${SYSBUILD_SOFTSIM_MODULE_DIR}/boards/thingy91x_nrf9151_pm_static.yml CACHE INTERNAL "")
    endif()
    return()
  endif()

  # Devicetree partitioning: board overlays only apply to the default image.
  # On the Thingy:91 X the SoftSIM factory layout differs from the stock board
  # devicetree, so MCUboot (and its S1 variant) must get the same partition
  # view -- with the stock view its primary slot would extend over the SoftSIM
  # storage area and a DFU swap would write the image trailer into it. The DKs
  # and the Thingy:91 need nothing here: their stock boot/slot0/slot1 geometry
  # already matches the SoftSIM layouts.
  if(SB_CONFIG_BOARD_THINGY91X_NRF9151_NS)
    foreach(image mcuboot mcuboot_s1_variant)
      if("${image}" IN_LIST PRE_CMAKE_IMAGES)
        # Guarded append (the nRF sysbuild module already puts its s0/s1
        # code-partition overlays in this variable, and the mcuboot entry is
        # never reset between reconfigures -- an unguarded append would add a
        # duplicate every CMake re-run).
        add_overlay_dts(${image}
          ${SYSBUILD_SOFTSIM_MODULE_DIR}/dts/softsim/thingy91x_softsim_partitions.dtsi)
      endif()
    endforeach()
  endif()
endfunction(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
