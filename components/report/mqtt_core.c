// Пакети MQTT 3.1.1 (phase8-session-report, ADR-0002): див. mqtt_core.h. Без заголовків ESP-IDF.
#include "mqtt_core.h"

#include <string.h>

#define PKT_CONNECT 0x10
#define PKT_CONNACK 0x20
#define PKT_PUBLISH_QOS1 0x32
#define PKT_PUBACK 0x40
#define PKT_DISCONNECT 0xE0

// Кодує «залишкову довжину» (1–4 байти); повертає кількість байтів.
static size_t put_remaining(uint8_t *out, size_t value)
{
    size_t i = 0;
    do {
        uint8_t b = (uint8_t)(value % 128u);
        value /= 128u;
        if (value > 0) {
            b |= 0x80;
        }
        out[i++] = b;
    } while (value > 0 && i < 4);
    return i;
}

static size_t put_string(uint8_t *out, const char *s, size_t len)
{
    out[0] = (uint8_t)(len >> 8);
    out[1] = (uint8_t)(len & 0xFF);
    memcpy(out + 2, s, len);
    return len + 2;
}

size_t mqtt_core_build_connect(uint8_t *buf, size_t n, const char *client_id, const char *user, const char *pass,
                               uint16_t keepalive_s)
{
    if (buf == NULL || client_id == NULL || (pass != NULL && user == NULL)) {
        return 0;
    }
    size_t cl = strlen(client_id);
    size_t ul = user ? strlen(user) : 0;
    size_t pl = pass ? strlen(pass) : 0;
    if (cl == 0 || cl > MQTT_CLIENT_ID_MAX || (user != NULL && (ul == 0 || ul > MQTT_CREDENTIAL_MAX)) ||
        (pass != NULL && pl > MQTT_CREDENTIAL_MAX)) {
        return 0;
    }
    size_t rem = 10 + 2 + cl + (user ? 2 + ul : 0) + (pass ? 2 + pl : 0);
    uint8_t hdr[5];
    hdr[0] = PKT_CONNECT;
    size_t hl = 1 + put_remaining(hdr + 1, rem);
    if (n < hl + rem) {
        return 0;
    }
    memcpy(buf, hdr, hl);
    size_t p = hl;
    static const uint8_t proto[] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04};
    memcpy(buf + p, proto, sizeof(proto));
    p += sizeof(proto);
    buf[p++] = (uint8_t)(0x02 | (user ? 0x80 : 0) | (pass ? 0x40 : 0));   // чиста сесія, ім'я, пароль
    buf[p++] = (uint8_t)(keepalive_s >> 8);
    buf[p++] = (uint8_t)(keepalive_s & 0xFF);
    p += put_string(buf + p, client_id, cl);
    if (user) {
        p += put_string(buf + p, user, ul);
    }
    if (pass) {
        p += put_string(buf + p, pass, pl);
    }
    return p;
}

size_t mqtt_core_build_publish(uint8_t *buf, size_t n, const char *topic, const uint8_t *payload, size_t len, uint16_t packet_id)
{
    if (buf == NULL || topic == NULL || packet_id == 0 || (len > 0 && payload == NULL) || len > MQTT_PAYLOAD_MAX) {
        return 0;
    }
    size_t tl = strlen(topic);
    if (tl == 0 || tl > MQTT_TOPIC_MAX || strchr(topic, '#') != NULL || strchr(topic, '+') != NULL) {
        return 0;
    }
    size_t rem = 2 + tl + 2 + len;
    uint8_t hdr[5];
    hdr[0] = PKT_PUBLISH_QOS1;
    size_t hl = 1 + put_remaining(hdr + 1, rem);
    if (n < hl + rem) {
        return 0;
    }
    memcpy(buf, hdr, hl);
    size_t p = hl;
    p += put_string(buf + p, topic, tl);
    buf[p++] = (uint8_t)(packet_id >> 8);
    buf[p++] = (uint8_t)(packet_id & 0xFF);
    if (len > 0) {
        memcpy(buf + p, payload, len);
        p += len;
    }
    return p;
}

size_t mqtt_core_build_disconnect(uint8_t *buf, size_t n)
{
    if (buf == NULL || n < 2) {
        return 0;
    }
    buf[0] = PKT_DISCONNECT;
    buf[1] = 0x00;
    return 2;
}

mqtt_status_t mqtt_core_packet_len(const uint8_t *buf, size_t len, size_t *total)
{
    if (buf == NULL || total == NULL) {
        return MQTT_MALFORMED;
    }
    size_t mult = 1, value = 0;
    for (size_t i = 1; i <= 4; i++) {
        if (len < i + 1) {
            return MQTT_INCOMPLETE;
        }
        uint8_t b = buf[i];
        value += (size_t)(b & 0x7F) * mult;
        if ((b & 0x80) == 0) {
            *total = 1 + i + value;
            return MQTT_OK;
        }
        mult *= 128;
    }
    return MQTT_MALFORMED;   // п'ятий байт довжини не допускається
}

// Фіксований пакет з двох байтів змінного заголовка: CONNACK і PUBACK.
static mqtt_status_t parse_fixed4(const uint8_t *buf, size_t len, uint8_t type)
{
    if (buf == NULL) {
        return MQTT_MALFORMED;
    }
    if (len >= 1 && buf[0] != type) {
        return MQTT_MALFORMED;
    }
    if (len >= 2 && buf[1] != 0x02) {
        return MQTT_MALFORMED;
    }
    if (len < 4) {
        return MQTT_INCOMPLETE;
    }
    return MQTT_OK;
}

mqtt_status_t mqtt_core_parse_connack(const uint8_t *buf, size_t len, uint8_t *rc)
{
    if (rc == NULL) {
        return MQTT_MALFORMED;
    }
    mqtt_status_t st = parse_fixed4(buf, len, PKT_CONNACK);
    if (st != MQTT_OK) {
        return st;
    }
    if ((buf[2] & 0xFE) != 0) {
        return MQTT_MALFORMED;   // зарезервовані біти прапорців
    }
    *rc = buf[3];
    return MQTT_OK;
}

mqtt_status_t mqtt_core_parse_puback(const uint8_t *buf, size_t len, uint16_t *packet_id)
{
    if (packet_id == NULL) {
        return MQTT_MALFORMED;
    }
    mqtt_status_t st = parse_fixed4(buf, len, PKT_PUBACK);
    if (st != MQTT_OK) {
        return st;
    }
    *packet_id = (uint16_t)((buf[2] << 8) | buf[3]);
    return MQTT_OK;
}

int mqtt_core_puback_matches(const uint8_t *buf, size_t len, uint16_t expected_id)
{
    uint16_t id = 0;
    return mqtt_core_parse_puback(buf, len, &id) == MQTT_OK && id == expected_id;
}

const char *mqtt_core_connack_text(uint8_t rc)
{
    switch (rc) {
    case 0:
        return "прийнято";
    case 1:
        return "сервер не підтримує версію протоколу";
    case 2:
        return "сервер відхилив ідентифікатор клієнта";
    case 3:
        return "сервер недоступний";
    case 4:
        return "невірні ім'я користувача чи пароль";
    case 5:
        return "авторизація не дозволена";
    default:
        return "невідомий код відповіді";
    }
}
