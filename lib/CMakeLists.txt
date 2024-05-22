zephyr_library()

include_directories(include)

zephyr_library_sources(nrf_softsim.c ss_fs.c provision.c ss_cache.c ss_heap.c ss_profile.c ss_crypto.c build_asserts.c )

zephyr_library_import(crypto ${CMAKE_CURRENT_SOURCE_DIR}/libcrypto.a)
zephyr_library_import(milenage ${CMAKE_CURRENT_SOURCE_DIR}/libmilenage.a)
zephyr_library_import(uicc ${CMAKE_CURRENT_SOURCE_DIR}/libuicc.a)
zephyr_library_import(storage ${CMAKE_CURRENT_SOURCE_DIR}/libstorage.a)


zephyr_include_directories(include)

add_definitions( -DCONFIG_CUSTOM_HEAP )
target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PRIVATE crypto  milenage  storage uicc)
