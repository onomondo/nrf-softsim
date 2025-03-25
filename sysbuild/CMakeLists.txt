zephyr_get(ZEPHYR_SDK_INSTALL_DIR)

set(ONOMONDO_SOFTSIM_BASE_ADDRESS $<TARGET_PROPERTY:partition_manager,PM_NVS_STORAGE_ADDRESS>)
set(ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT ${CMAKE_BINARY_DIR}/onomondo-softsim/template.hex)

add_custom_command(
  OUTPUT template.hex
  COMMAND ${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi-objcopy
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

function(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
  # Set the default NCS static partition layout for the Thingy:91
  if(SB_CONFIG_THINGY91_STATIC_PARTITIONS_FACTORY)
    set(PM_STATIC_YML_FILE ${SYSBUILD_SOFTSIM_MODULE_DIR}/boards/thingy91_pm_static.yml CACHE INTERNAL "")
  endif()

  # Set the default NCS static partition layout for the Thingy:91 X
  if(SB_CONFIG_BOARD_THINGY91X_NRF9151_NS AND SB_CONFIG_THINGY91X_STATIC_PARTITIONS_FACTORY)
    set(PM_STATIC_YML_FILE ${SYSBUILD_SOFTSIM_MODULE_DIR}/boards/thingy91x_nrf9151_pm_static.yml CACHE INTERNAL "")
  endif()
endfunction(${SYSBUILD_CURRENT_MODULE_NAME}_pre_cmake)
