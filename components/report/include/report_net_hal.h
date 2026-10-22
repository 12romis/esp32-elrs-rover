// HAL мережі звіту (phase8-session-report, ADR-0002, ADR-0003, ADR-0006): Wi-Fi вгору й повністю вниз на кожну спробу, TLS із
// закріпленим кореневим сертифікатом, обмін MQTT 3.1.1 (CONNECT → CONNACK → PUBLISH QoS 1 → PUBACK → DISCONNECT) кроками не
// довшими за 100 мс. Пароль і SSID нічого не друкує. Усі виклики — з задачі report.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "report_queue_core.h"

// Колбек «спробу ще дозволено» (стан DISARMED, ліміт часу не вичерпано). Мережевий код опитує його не рідше ніж раз на
// 100 мс; false — перервати: з'єднання закривається, Wi-Fi вимикається.
typedef bool (*report_net_allow_fn)(void);

// Параметри спроби.
typedef struct {
    report_net_allow_fn allow;
    uint32_t start_ms;          // початок спроби (мс від старту): ліміти Wi-Fi 15 с, TLS+MQTT 10 с, усього 30 с
    char mac[13];               // MAC у нижньому регістрі без роздільників: топік rover/<mac>/session, client id rover-<mac>
} report_net_t;

// Один раз при старті задачі: esp_netif і цикл подій.
void report_net_init(void);

// Налаштування мережі задано: є report_secrets.h з непорожніми SSID, адресою й іменем користувача, і кореневий сертифікат.
bool report_net_config_ok(void);
// Що саме не задано, без значень: «немає report_secrets.h», «порожні SSID чи адреса сервера», «немає кореневого сертифіката».
const char *report_net_config_problem(void);

// Піднімає Wi-Fi і чекає IP. REPORT_OUT_NONE — підключено; NO_NETWORK, TIMEOUT чи ABORTED — Wi-Fi вже вимкнено.
report_outcome_t report_net_wifi_up(const report_net_t *ctx);

// Відкриває TLS (перевірка сертифіката сервера обов'язкова) і виконує CONNECT/CONNACK. REPORT_OUT_NONE — готово до публікації;
// CERT — сертифікат недовірений (CONNECT не надсилався); NO_NETWORK, NO_ACK (відмова CONNACK), TIMEOUT, ABORTED.
report_outcome_t report_net_mqtt_open(const report_net_t *ctx);

// Публікує документ (QoS 1) і чекає PUBACK з тим самим packet id. DELIVERED, NO_ACK, TIMEOUT чи ABORTED.
report_outcome_t report_net_mqtt_publish(const report_net_t *ctx, const char *json, size_t len, uint16_t packet_id);

// DISCONNECT і закриття TLS (безпечно викликати завжди). Не вимикає Wi-Fi.
void report_net_mqtt_close(void);

// Повне вимкнення Wi-Fi і звільнення буферів (безпечно викликати завжди).
void report_net_wifi_down(void);

// Вільна внутрішня памʼять, байт (для status і журналу перевірки повернення памʼяті).
uint32_t report_net_free_heap(void);
