// Пакети MQTT 3.1.1 (phase8-session-report, ADR-0002): кодування CONNECT, PUBLISH (QoS 1), DISCONNECT і розбір CONNACK та
// PUBACK. Чисті функції над байтами: без заголовків ESP-IDF, без мережі; обмін по TLS робить report_net_hal.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MQTT_PAYLOAD_MAX 1024    // найбільше тіло PUBLISH (збігається з REPORT_JSON_MAX)
#define MQTT_TOPIC_MAX 128
#define MQTT_CLIENT_ID_MAX 64
#define MQTT_CREDENTIAL_MAX 128  // ім'я користувача і пароль
#define MQTT_PACKET_MAX (MQTT_PAYLOAD_MAX + MQTT_TOPIC_MAX + 16)   // найбільший пакет, який будує кодер

typedef enum {
    MQTT_OK,
    MQTT_INCOMPLETE,   // пакет обрізаний: потрібно більше байтів
    MQTT_MALFORMED     // не той тип пакета чи недійсна довжина
} mqtt_status_t;

// Коди відповіді CONNACK (3.1.1): 0 прийнято, 1…5 відмови.
#define MQTT_CONNACK_ACCEPTED 0

// Кодувальники повертають довжину пакета або 0 за помилки (малий буфер, порожній чи задовгий аргумент, недозволені символи).
// CONNECT: чиста сесія, ім'я користувача й пароль (user == NULL — без автентифікації; пароль без імені заборонений).
size_t mqtt_core_build_connect(uint8_t *buf, size_t n, const char *client_id, const char *user, const char *pass,
                               uint16_t keepalive_s);
// PUBLISH: QoS 1, DUP і retain вимкнені; packet_id ≠ 0; тема без символів-замінників '#' і '+'; payload ≤ MQTT_PAYLOAD_MAX.
size_t mqtt_core_build_publish(uint8_t *buf, size_t n, const char *topic, const uint8_t *payload, size_t len, uint16_t packet_id);
size_t mqtt_core_build_disconnect(uint8_t *buf, size_t n);

// Скільки байтів займає весь пакет за його початком (фіксований заголовок з довжиною, що залишилась). MQTT_INCOMPLETE —
// заголовок ще не повний; *total — повна довжина пакета.
mqtt_status_t mqtt_core_packet_len(const uint8_t *buf, size_t len, size_t *total);

// CONNACK: MQTT_OK і *rc (код відповіді; 0 — прийнято). Обрізаний — MQTT_INCOMPLETE; чужий тип чи довжина — MQTT_MALFORMED.
mqtt_status_t mqtt_core_parse_connack(const uint8_t *buf, size_t len, uint8_t *rc);
// PUBACK: MQTT_OK і *packet_id.
mqtt_status_t mqtt_core_parse_puback(const uint8_t *buf, size_t len, uint16_t *packet_id);
// Підтвердження саме цього пакета: розібраний PUBACK з очікуваним packet id.
int mqtt_core_puback_matches(const uint8_t *buf, size_t len, uint16_t expected_id);

// Пояснення коду CONNACK українською для журналу (без секретів).
const char *mqtt_core_connack_text(uint8_t rc);
