idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_state rover_console watchdog storage
                       PRIV_REQUIRES log)
