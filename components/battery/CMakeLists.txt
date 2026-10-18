# Фаза 1: чиста логіка (battery_core.c) і HAL вимірювання напруги (battery_hal.c).
idf_component_register(SRCS "battery_core.c" "battery_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board
                       PRIV_REQUIRES esp_adc esp_timer freertos log)
