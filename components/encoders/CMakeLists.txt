# Фаза 4: encoders_core.c (чиста логіка лічильників, швидкості й тексту знімка) і encoders_hal.c (блоки лічильника,
# задача encoders, стан під portMUX).
idf_component_register(SRCS "encoders_core.c" "encoders_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES board esp_driver_pcnt esp_timer esp_system freertos log)
