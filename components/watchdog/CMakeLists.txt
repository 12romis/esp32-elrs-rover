idf_component_register(SRCS "watchdog_core.c" "watchdog_hal.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_system freertos log)
