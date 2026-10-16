idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board esp_adc esp_driver_i2c freertos esp_system esp_hw_support esp_psram spi_flash)
