idf_component_register(SRCS "storage_core.c" "storage_hal.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash log)
