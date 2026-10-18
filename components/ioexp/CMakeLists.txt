# Фаза 2: ioexp_core.c (чиста логіка) і ioexp_hal.c (запис байта, старт: нуль першим).
idf_component_register(SRCS "ioexp_core.c" "ioexp_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board i2c_bus esp_driver_i2c
                       PRIV_REQUIRES esp_timer log)
