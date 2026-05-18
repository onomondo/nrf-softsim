zephyr_library()

set(_softsim_libs uicc milenage crypto storage)
set(_softsim_install_dir ${CMAKE_CURRENT_SOURCE_DIR}/ss_lib_build)

function(_softsim_default_bool opt default doc)
  if(NOT DEFINED ${opt})
    set(${opt} ${default} CACHE BOOL "${doc}")
  endif()
endfunction()

function(_softsim_set_uicc_bool uicc_opt softsim_opt default doc)
  if(DEFINED ${softsim_opt})
    if("${${softsim_opt}}" STREQUAL "y")
      set(${uicc_opt} ON CACHE BOOL "${doc}" FORCE)
    else()
      set(${uicc_opt} OFF CACHE BOOL "${doc}" FORCE)
    endif()
  else()
    _softsim_default_bool(${uicc_opt} ${default} "${doc}")
  endif()
endfunction()

message(STATUS "SoftSIM: Building static libraries from onomondo-uicc")
message(STATUS "SoftSIM: onomondo-uicc library install dir: ${_softsim_install_dir}")
file(MAKE_DIRECTORY ${_softsim_install_dir})

# Set default option values in one place.
_softsim_set_uicc_bool(CONFIG_ENABLE_SANITIZE CONFIG_SOFTSIM_UICC_ENABLE_SANITIZE OFF "Enable -fsanitize in onomondo-uicc build")
_softsim_set_uicc_bool(CONFIG_USE_SYSTEM_HEAP CONFIG_SOFTSIM_UICC_USE_SYSTEM_HEAP OFF "Use system malloc/free in onomondo-uicc")
_softsim_set_uicc_bool(CONFIG_USE_LOGS CONFIG_SOFTSIM_UICC_USE_LOGS ON "Enable ss_logp logging in onomondo-uicc")
_softsim_set_uicc_bool(CONFIG_USE_EXPERIMENTAL_SUSPEND_COMMAND CONFIG_SOFTSIM_UICC_USE_EXPERIMENTAL_SUSPEND_COMMAND ON "Enable experimental suspend command in onomondo-uicc")
_softsim_set_uicc_bool(CONFIG_EXTERNAL_CRYPTO_IMPL CONFIG_SOFTSIM_UICC_EXTERNAL_CRYPTO_IMPL ON "Use external crypto implementation in onomondo-uicc")
_softsim_set_uicc_bool(CONFIG_EXTERNAL_KEY_LOAD CONFIG_SOFTSIM_UICC_EXTERNAL_KEY_LOAD OFF "Enable external key loader in onomondo-uicc")
_softsim_set_uicc_bool(CONFIG_USE_UTILS CONFIG_SOFTSIM_UICC_USE_UTILS OFF "Build onomondo-uicc utils target")
_softsim_set_uicc_bool(CONFIG_NO_DEFAULT_IMPL CONFIG_SOFTSIM_UICC_NO_DEFAULT_IMPL ON "Build onomondo-uicc with no default impl")
_softsim_set_uicc_bool(CONFIG_COMPACT_STORAGE CONFIG_SOFTSIM_UICC_COMPACT_STORAGE ON "Build with compact storage implementation")
_softsim_set_uicc_bool(CONFIG_BUILD_LIB_ONLY CONFIG_SOFTSIM_UICC_BUILD_LIB_ONLY ON "Only build libraries in onomondo-uicc")

# Detect uninitialized git submodule early and emit an actionable error.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/onomondo-uicc/CMakeLists.txt")
  message(FATAL_ERROR
    "SoftSIM: onomondo-uicc submodule is not initialized.\n"
    "Run:\n"
    "  git -C ${CMAKE_CURRENT_SOURCE_DIR}/.. submodule update --init\n"
    "Or, if onomondo-softsim is a west project in your application, add\n"
    "  submodules: true\n"
    "to its entry in your west.yml and re-run 'west update'.")
endif()

message(STATUS "SoftSIM: Configuring onomondo-uicc (in-tree)")
add_subdirectory(onomondo-uicc)

foreach(_t IN LISTS _softsim_libs utils)
  if(TARGET ${_t})
    # Propagate Zephyr compile definitions and options to subproject targets.
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

foreach(_t IN LISTS _softsim_libs)
  if(NOT TARGET ${_t})
    message(FATAL_ERROR "SoftSIM: Expected in-tree target '${_t}' was not created by onomondo-uicc")
  endif()
  target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PRIVATE ${_t})
endforeach()

zephyr_include_directories(include)
zephyr_include_directories(onomondo-uicc/include)

zephyr_library_sources(
  nrf_softsim.c
  ss_fs.c
  ss_profile.c
  ss_provision.c
  ss_cache.c
  ss_heap.c
  ss_crypto.c
  ss_logp_zephyr.c
  build_asserts.c
)

target_compile_definitions(${ZEPHYR_CURRENT_LIBRARY} PRIVATE CONFIG_CUSTOM_HEAP)
