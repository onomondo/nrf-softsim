zephyr_library()

set(_softsim_libs uicc milenage crypto storage)
set(_softsim_install_dir ${CMAKE_CURRENT_SOURCE_DIR}/ss_lib_build)
set(_softsim_libdir ${_softsim_install_dir})
set(_softsim_built_in_tree OFF)

function(_softsim_default_bool opt default doc)
  if(NOT DEFINED ${opt})
    set(${opt} ${default} CACHE BOOL "${doc}")
  endif()
endfunction()

message(STATUS "[SoftSIM] : ----------Building Custom SoftSIM Static Libraries from onomondo-uicc----------")
message(STATUS "[SoftSIM] : Using onomondo-uicc library install dir: ${_softsim_install_dir}")
file(MAKE_DIRECTORY ${_softsim_install_dir})

# Keep option defaults in one place.
_softsim_default_bool(CONFIG_ENABLE_SANITIZE OFF "Enable -fsanitize in onomondo-uicc build")
_softsim_default_bool(CONFIG_USE_SYSTEM_HEAP OFF "Use system malloc/free in onomondo-uicc")
_softsim_default_bool(CONFIG_USE_LOGS ON "Enable ss_logp logging in onomondo-uicc")
_softsim_default_bool(CONFIG_USE_EXPERIMENTAL_SUSPEND_COMMAND ON "Enable experimental suspend command in onomondo-uicc")
_softsim_default_bool(CONFIG_EXTERNAL_CRYPTO_IMPL ON "Use external crypto implementation in onomondo-uicc")
_softsim_default_bool(CONFIG_EXTERNAL_KEY_LOAD OFF "Enable external key loader in onomondo-uicc")
_softsim_default_bool(CONFIG_USE_UTILS OFF "Build onomondo-uicc utils target")
_softsim_default_bool(CONFIG_NO_DEFAULT_IMPL ON "Build onomondo-uicc with no default impl")
_softsim_default_bool(CONFIG_COMPACT_STORAGE ON "Build with compact storage implementation")
_softsim_default_bool(CONFIG_BUILD_LIB_ONLY ON "Only build libraries in onomondo-uicc")

message(STATUS "[SoftSIM] : Initiating build from onomondo-uicc (in-tree)")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${_softsim_install_dir})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${_softsim_install_dir})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${_softsim_install_dir})
add_subdirectory(onomondo-uicc)
set(_softsim_built_in_tree ON)
set(_softsim_libdir ${_softsim_install_dir})

if(_softsim_built_in_tree)
  foreach(_t IN LISTS _softsim_libs utils)
    if(TARGET ${_t})
      # Propagate Zephyr compile flags/definitions to the subproject targets.
      target_compile_definitions(${_t} PRIVATE
        $<TARGET_PROPERTY:${ZEPHYR_CURRENT_LIBRARY},COMPILE_DEFINITIONS>
        $<TARGET_PROPERTY:${ZEPHYR_CURRENT_LIBRARY},INTERFACE_COMPILE_DEFINITIONS>
      )
      target_compile_options(${_t} PRIVATE
        $<TARGET_PROPERTY:${ZEPHYR_CURRENT_LIBRARY},COMPILE_OPTIONS>
        $<TARGET_PROPERTY:${ZEPHYR_CURRENT_LIBRARY},INTERFACE_COMPILE_OPTIONS>
      )
    endif()
  endforeach()
endif()

foreach(_t IN LISTS _softsim_libs)
  if(TARGET ${_t})
    target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${_t})
  else()
    zephyr_library_import(${_t} ${_softsim_libdir}/lib${_t}.a)
    target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${_t})
  endif()
endforeach()

zephyr_include_directories(include)
zephyr_include_directories(onomondo-uicc/include) # expose onomondo-uicc headers regardless of build mode

zephyr_library_sources(nrf_softsim.c ss_fs.c ss_profile.c ss_provision.c ss_cache.c ss_heap.c ss_crypto.c ss_logp_zephyr.c build_asserts.c)

add_definitions(-DCONFIG_CUSTOM_HEAP)
