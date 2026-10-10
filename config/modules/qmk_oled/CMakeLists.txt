if(CONFIG_CHOCOFI_QMK_OLED)
  zephyr_library()
  zephyr_library_sources(dashboard.c)
endif()
