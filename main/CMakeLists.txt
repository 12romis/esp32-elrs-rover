idf_component_register(SRCS "main.c" "app_states.c"
                       INCLUDE_DIRS "."
                       REQUIRES board nvs_flash esp_timer)
