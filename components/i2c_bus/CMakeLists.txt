# Фаза 2: i2c_bus_core.c (чиста логіка скану) і i2c_bus_hal.c (шина i2c_master, мʼютекс m_i2c).
idf_component_register(SRCS "i2c_bus_core.c" "i2c_bus_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES board freertos log)
