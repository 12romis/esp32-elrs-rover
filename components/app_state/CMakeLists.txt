idf_component_register(SRCS "app_state_core.c" "app_state_hal.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_timer freertos log)
