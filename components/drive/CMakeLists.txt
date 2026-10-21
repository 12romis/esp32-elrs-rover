# Фаза 1: drive_core.c (чиста логіка) і drive_hal.c (LEDC, GPIO, задача control); фаза 5 — регулятор (pid, encoders).
idf_component_register(SRCS "drive_core.c" "drive_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_state battery crsf encoders pid tof
                       PRIV_REQUIRES board esp_driver_ledc esp_driver_gpio esp_timer esp_system freertos log)
