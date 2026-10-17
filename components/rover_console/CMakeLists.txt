idf_component_register(SRCS "console_core.c" "console_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_state esp_driver_usb_serial_jtag
                       PRIV_REQUIRES esp_timer esp_system freertos log heap)
