# Фаза 6: tof_core.c (чиста логіка далекомірів) і tof_hal.c (драйвер VL53L0X, послідовність лінії скидання; задача sensors — T11).
idf_component_register(SRCS "tof_core.c" "tof_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES board ioexp i2c_bus app_state esp_driver_i2c esp_rom freertos log esp_timer)
