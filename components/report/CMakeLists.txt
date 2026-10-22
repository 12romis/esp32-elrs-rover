# Фаза 8: звіт сесії. Чиста логіка: report_core.c (документ), mqtt_core.c (пакети MQTT 3.1.1), report_queue_core.c (черга,
# правила спроби, формат запису). HAL: report_store_hal.c (NVS), report_net_hal.c (Wi-Fi, TLS, обмін), report_hal.c (задача).
# Кореневий сертифікат certs/root_ca.pem вбудовується лише якщо файл існує; локальні налаштування report_secrets.h
# підключаються через __has_include. Без них збірка проходить, а передача вимкнена (ADR-0006, spec A19, AC-24).
set(report_embed "")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/certs/root_ca.pem")
    set(report_embed "certs/root_ca.pem")
endif()

idf_component_register(SRCS "report_core.c" "mqtt_core.c" "report_queue_core.c"
                            "report_store_hal.c" "report_net_hal.c" "report_hal.c"
                       INCLUDE_DIRS "include"
                       REQUIRES session app_state battery
                       PRIV_REQUIRES nvs_flash esp_wifi esp-tls esp_netif esp_event freertos log esp_app_format esp_timer
                                     esp_hw_support
                       EMBED_TXTFILES ${report_embed})

if(report_embed)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE REPORT_HAVE_ROOT_CA=1)
endif()
