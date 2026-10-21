# Фаза 7: telemetry_core.c (чиста логіка кадру й статистики) і telemetry_hal.c (задача telemetry, запис через crsf).
idf_component_register(SRCS "telemetry_core.c" "telemetry_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES battery crsf
                       PRIV_REQUIRES esp_timer freertos log)
