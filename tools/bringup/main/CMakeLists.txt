idf_component_register(SRCS "main.c" "p0_chip.c" "p1_i2c_scan.c" "p2_pcf_outputs.c" "p3_adc_battery.c" "p4_tof.c" "p5_crsf.c" "p6_encoders.c" "p7_motors.c" "p8_indicator.c" "p9_buzzer.c"
                       INCLUDE_DIRS "."
                       REQUIRES board esp_adc esp_driver_i2c esp_driver_uart esp_driver_pcnt
                                esp_driver_ledc esp_driver_gpio esp_driver_rmt freertos esp_timer
                                esp_system esp_hw_support esp_psram spi_flash)
