idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_state rover_console watchdog storage drive battery ioexp ui encoders pid tof telemetry session report
                       PRIV_REQUIRES log)
