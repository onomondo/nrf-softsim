add_subdirectory_ifdef(CONFIG_SOFTSIM lib)

# Override the default NCS static partition layout for the Thingy91
if(CONFIG_THINGY91_STATIC_PARTITIONS_FACTORY)
  set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_LIST_DIR}/thingy91_pm_static.yml CACHE INTERNAL "")
endif()
