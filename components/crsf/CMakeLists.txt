# Фаза 3: crsf_core.c (чиста логіка кадрів і каналів) і crsf_hal.c (UART2, задача crsf_rx, знімок).
idf_component_register(SRCS "crsf_core.c" "crsf_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES board esp_driver_uart esp_timer esp_system freertos log)
