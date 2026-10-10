if(CONFIG_CHOCOFI_QMK_OLED)
  zephyr_library()
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(dashboard.c)
  # Keycode events are only built on the central (or a standalone keyboard).
  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(case_keys.c)
  endif()
endif()
