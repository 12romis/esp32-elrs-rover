# Фаза 5: pid_core.c (чиста логіка регулятора) і pid_hal.c (набір під portMUX, NVS, задача pid_trace).
idf_component_register(SRCS "pid_core.c" "pid_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES app_state nvs_flash freertos log esp_system)
