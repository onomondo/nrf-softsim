zephyr_library()

include_directories(include)

zephyr_library_sources(nrf_softsim.c fs_port.c storage.c)

zephyr_library_import(crypto ${CMAKE_CURRENT_SOURCE_DIR}/libcrypto.a)
zephyr_library_import(milenage ${CMAKE_CURRENT_SOURCE_DIR}/libmilenage.a)
zephyr_library_import(uicc ${CMAKE_CURRENT_SOURCE_DIR}/libuicc.a)

target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PRIVATE crypto milenage uicc)