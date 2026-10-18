# Фаза 2: ui_core.c (чиста логіка) і ui_hal.c (задача ui, зумер, порт індикатора).
idf_component_register(SRCS "ui_core.c" "ui_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_state drive ioexp
                       PRIV_REQUIRES board esp_driver_ledc esp_driver_rmt esp_timer freertos log esp_system)
