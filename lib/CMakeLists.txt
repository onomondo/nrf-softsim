zephyr_library()

include_directories(include)

zephyr_library_sources(nrf_softsim.c provision.c  heap_port.c profile.c crypto_port.c build_asserts.c )

if (CONFIG_SOFTSIM_LITTLE_FS_BACKEND)
  zephyr_library_sources(ss_littlefs.c)
else()
  zephyr_library_sources(fs_port.c f_cache.c)
endif()

zephyr_library_import(crypto ${CMAKE_CURRENT_SOURCE_DIR}/libcrypto.a)
zephyr_library_import(milenage ${CMAKE_CURRENT_SOURCE_DIR}/libmilenage.a)
zephyr_library_import(uicc ${CMAKE_CURRENT_SOURCE_DIR}/libuicc.a)
zephyr_library_import(storage ${CMAKE_CURRENT_SOURCE_DIR}/libstorage.a)


zephyr_include_directories(include)

add_definitions( -DCONFIG_CUSTOM_HEAP )
target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PRIVATE crypto  milenage  storage uicc)
