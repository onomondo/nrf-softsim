add_subdirectory_ifdef(CONFIG_SOFTSIM lib)

set(ONOMONDO_SOFTSIM_BASE_ADDRESS $<TARGET_PROPERTY:partition_manager,PM_NVS_STORAGE_ADDRESS>)
set(ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT ${CMAKE_BINARY_DIR}/onomondo-softsim/template.hex)

if(CONFIG_SOFTSIM)

add_custom_command(
  OUTPUT template.hex
  COMMAND ${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi-objcopy
  --input-target=binary
  --output-target=ihex
  --change-address ${ONOMONDO_SOFTSIM_BASE_ADDRESS}
  ${CMAKE_CURRENT_LIST_DIR}/lib/profile/template.bin
  ${ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT}
  BYPRODUCTS ${ONOMONDO_SOFTSIM_TEMPLATE_OUTPUT} # This is needed for Ninja
)
add_custom_target(onomondo_softsim_template DEPENDS template.hex)

if(CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX)
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
endif()
