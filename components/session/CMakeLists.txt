# Фаза 8: session_core.c (чиста логіка сесії й статистики) і session_hal.c (черга підсумків між control і report).
idf_component_register(SRCS "session_core.c" "session_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES app_state battery encoders
                       PRIV_REQUIRES freertos log)
