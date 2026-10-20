# Фаза 6: tof_core.c (чиста логіка далекомірів) і tof_hal.c (драйвер, задача sensors; додає T10).
idf_component_register(SRCS "tof_core.c"
                       INCLUDE_DIRS "include")
