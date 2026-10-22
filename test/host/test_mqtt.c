// Хостові тести mqtt_core (phase8-session-report T8; ADR-0002; AC-10, AC-22): еталонні байти MQTT 3.1.1.
#include <string.h>

#include "mqtt_core.h"
#include "test_support.h"

// Чи містить буфер підрядок (memmem є лише в розширенні GNU).
static int contains(const uint8_t *h, size_t hn, const char *needle)
{
    size_t m = strlen(needle);
    for (size_t i = 0; i + m <= hn; i++) {
        if (memcmp(h + i, needle, m) == 0) {
            return 1;
        }
    }
    return 0;
}

static int eq(const uint8_t *got, size_t n, const uint8_t *want, size_t m)
{
    return n == m && memcmp(got, want, m) == 0;
}

static void test_connect_reference(void)
{
    uint8_t buf[MQTT_PACKET_MAX];
    size_t n = mqtt_core_build_connect(buf, sizeof(buf), "rover-aabbccddeeff", "u", "p", 0);
    const uint8_t want[] = {0x10, 0x24, 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0xC2, 0x00, 0x00, 0x00, 0x12,
                            'r', 'o', 'v', 'e', 'r', '-', 'a', 'a', 'b', 'b', 'c', 'c', 'd', 'd', 'e', 'e', 'f', 'f',
                            0x00, 0x01, 'u', 0x00, 0x01, 'p'};
    CHECK(eq(buf, n, want, sizeof(want)));
    // без автентифікації: прапорець тільки чистої сесії
    n = mqtt_core_build_connect(buf, sizeof(buf), "c", NULL, NULL, 60);
    const uint8_t want2[] = {0x10, 0x0D, 0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x3C, 0x00, 0x01, 'c'};
    CHECK(eq(buf, n, want2, sizeof(want2)));
    // лише ім'я користувача
    n = mqtt_core_build_connect(buf, sizeof(buf), "c", "ab", NULL, 0);
    CHECK(n > 0 && buf[9] == 0x82);
}

static void test_connect_rejects_bad_args(void)
{
    uint8_t buf[MQTT_PACKET_MAX];
    CHECK(mqtt_core_build_connect(buf, sizeof(buf), NULL, "u", "p", 0) == 0);
    CHECK(mqtt_core_build_connect(buf, sizeof(buf), "", "u", "p", 0) == 0);
    CHECK(mqtt_core_build_connect(buf, sizeof(buf), "c", NULL, "p", 0) == 0);          // пароль без імені
    CHECK(mqtt_core_build_connect(NULL, 0, "c", "u", "p", 0) == 0);
    char longid[MQTT_CLIENT_ID_MAX + 2];
    memset(longid, 'x', sizeof(longid) - 1);
    longid[sizeof(longid) - 1] = '\0';
    CHECK(mqtt_core_build_connect(buf, sizeof(buf), longid, "u", "p", 0) == 0);
    CHECK(mqtt_core_build_connect(buf, 10, "rover-aabbccddeeff", "u", "p", 0) == 0);  // малий буфер
    // буфер рівно на пакет — гаразд; на байт менший — ні
    size_t n = mqtt_core_build_connect(buf, sizeof(buf), "c", "u", "p", 0);
    CHECK(n > 0 && mqtt_core_build_connect(buf, n, "c", "u", "p", 0) == n);
    CHECK(mqtt_core_build_connect(buf, n - 1, "c", "u", "p", 0) == 0);
}

static void test_publish_reference(void)
{
    uint8_t buf[MQTT_PACKET_MAX];
    const uint8_t payload[] = {'h', 'i'};
    size_t n = mqtt_core_build_publish(buf, sizeof(buf), "a/b", payload, sizeof(payload), 0x0102);
    const uint8_t want[] = {0x32, 0x09, 0x00, 0x03, 'a', '/', 'b', 0x01, 0x02, 'h', 'i'};
    CHECK(eq(buf, n, want, sizeof(want)));
    // порожнє тіло
    n = mqtt_core_build_publish(buf, sizeof(buf), "t", NULL, 0, 1);
    const uint8_t want2[] = {0x32, 0x05, 0x00, 0x01, 't', 0x00, 0x01};
    CHECK(eq(buf, n, want2, sizeof(want2)));
}

static void test_publish_remaining_length_multibyte(void)
{
    uint8_t buf[MQTT_PACKET_MAX];
    uint8_t payload[MQTT_PAYLOAD_MAX];
    memset(payload, 'x', sizeof(payload));
    // тіло 200 байтів: залишилось 2+3+2+200 = 207 → 0xCF 0x01
    size_t n = mqtt_core_build_publish(buf, sizeof(buf), "a/b", payload, 200, 5);
    CHECK(n == 1 + 2 + 207 && buf[0] == 0x32 && buf[1] == 0xCF && buf[2] == 0x01);
    // найбільше тіло 1024: залишилось 2+3+2+1024 = 1031 → 0x87 0x08
    n = mqtt_core_build_publish(buf, sizeof(buf), "a/b", payload, 1024, 5);
    CHECK(n == 1 + 2 + 1031 && buf[1] == 0x87 && buf[2] == 0x08);
    // більше межі — відмова
    uint8_t big[MQTT_PAYLOAD_MAX + 1];
    memset(big, 'x', sizeof(big));
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), "a/b", big, sizeof(big), 5) == 0);
}

static void test_publish_rejects_bad_args(void)
{
    uint8_t buf[MQTT_PACKET_MAX];
    const uint8_t p[] = {'x'};
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), "a/b", p, 1, 0) == 0);             // packet id 0 заборонений
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), NULL, p, 1, 1) == 0);
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), "", p, 1, 1) == 0);
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), "a/#", p, 1, 1) == 0);             // символи-замінники
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), "a/+/b", p, 1, 1) == 0);
    CHECK(mqtt_core_build_publish(buf, sizeof(buf), "a/b", NULL, 1, 1) == 0);          // довжина без даних
    CHECK(mqtt_core_build_publish(buf, 5, "a/b", p, 1, 1) == 0);                       // малий буфер
    CHECK(mqtt_core_build_publish(NULL, 0, "a/b", p, 1, 1) == 0);
}

static void test_disconnect(void)
{
    uint8_t buf[4];
    size_t n = mqtt_core_build_disconnect(buf, sizeof(buf));
    const uint8_t want[] = {0xE0, 0x00};
    CHECK(eq(buf, n, want, sizeof(want)));
    CHECK(mqtt_core_build_disconnect(buf, 1) == 0 && mqtt_core_build_disconnect(NULL, 0) == 0);
}

static void test_connack(void)
{
    uint8_t rc = 99;
    const uint8_t ok[] = {0x20, 0x02, 0x00, 0x00};
    CHECK(mqtt_core_parse_connack(ok, sizeof(ok), &rc) == MQTT_OK && rc == 0);
    const uint8_t bad_creds[] = {0x20, 0x02, 0x00, 0x04};
    CHECK(mqtt_core_parse_connack(bad_creds, sizeof(bad_creds), &rc) == MQTT_OK && rc == 4);
    const uint8_t unauth[] = {0x20, 0x02, 0x00, 0x05};
    CHECK(mqtt_core_parse_connack(unauth, sizeof(unauth), &rc) == MQTT_OK && rc == 5);
    // обрізані
    CHECK(mqtt_core_parse_connack(ok, 0, &rc) == MQTT_INCOMPLETE);
    CHECK(mqtt_core_parse_connack(ok, 1, &rc) == MQTT_INCOMPLETE);
    CHECK(mqtt_core_parse_connack(ok, 3, &rc) == MQTT_INCOMPLETE);
    // чужий тип чи довжина
    const uint8_t puback[] = {0x40, 0x02, 0x00, 0x01};
    CHECK(mqtt_core_parse_connack(puback, sizeof(puback), &rc) == MQTT_MALFORMED);
    const uint8_t badlen[] = {0x20, 0x03, 0x00, 0x00, 0x00};
    CHECK(mqtt_core_parse_connack(badlen, sizeof(badlen), &rc) == MQTT_MALFORMED);
    const uint8_t badflags[] = {0x20, 0x02, 0x02, 0x00};     // зарезервовані біти прапорців
    CHECK(mqtt_core_parse_connack(badflags, sizeof(badflags), &rc) == MQTT_MALFORMED);
    CHECK(mqtt_core_parse_connack(NULL, 4, &rc) == MQTT_MALFORMED && mqtt_core_parse_connack(ok, 4, NULL) == MQTT_MALFORMED);
    CHECK(strlen(mqtt_core_connack_text(0)) > 0 && strlen(mqtt_core_connack_text(4)) > 0 && strlen(mqtt_core_connack_text(200)) > 0);
}

static void test_puback(void)
{
    uint16_t id = 0;
    const uint8_t p[] = {0x40, 0x02, 0x12, 0x34};
    CHECK(mqtt_core_parse_puback(p, sizeof(p), &id) == MQTT_OK && id == 0x1234);
    CHECK(mqtt_core_puback_matches(p, sizeof(p), 0x1234));
    CHECK(!mqtt_core_puback_matches(p, sizeof(p), 0x1235));          // чужий packet id — не підтвердження
    CHECK(mqtt_core_parse_puback(p, 3, &id) == MQTT_INCOMPLETE);
    CHECK(!mqtt_core_puback_matches(p, 3, 0x1234));
    const uint8_t wrong[] = {0x20, 0x02, 0x00, 0x00};
    CHECK(mqtt_core_parse_puback(wrong, sizeof(wrong), &id) == MQTT_MALFORMED);
    const uint8_t longer[] = {0x40, 0x03, 0x12, 0x34, 0x00};
    CHECK(mqtt_core_parse_puback(longer, sizeof(longer), &id) == MQTT_MALFORMED);
    CHECK(!mqtt_core_puback_matches(wrong, sizeof(wrong), 0));
}

static void test_packet_len(void)
{
    size_t total = 0;
    const uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
    CHECK(mqtt_core_packet_len(connack, 1, &total) == MQTT_INCOMPLETE);
    CHECK(mqtt_core_packet_len(connack, 2, &total) == MQTT_OK && total == 4);
    const uint8_t multi[] = {0x32, 0x87, 0x08};
    CHECK(mqtt_core_packet_len(multi, 2, &total) == MQTT_INCOMPLETE);
    CHECK(mqtt_core_packet_len(multi, 3, &total) == MQTT_OK && total == 1 + 2 + 1031);
    const uint8_t overlong[] = {0x32, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};      // п'ять байтів довжини — недійсно
    CHECK(mqtt_core_packet_len(overlong, sizeof(overlong), &total) == MQTT_MALFORMED);
    CHECK(mqtt_core_packet_len(NULL, 0, &total) == MQTT_MALFORMED);
}

static void test_password_only_in_connect(void)
{
    uint8_t conn[MQTT_PACKET_MAX], pub[MQTT_PACKET_MAX], dis[8];
    const char *pass = "S3cretPass";
    size_t cn = mqtt_core_build_connect(conn, sizeof(conn), "rover-aabbccddeeff", "rover", pass, 0);
    const uint8_t payload[] = "{\"seq\":1}";
    size_t pn = mqtt_core_build_publish(pub, sizeof(pub), "rover/aabbccddeeff/session", payload, sizeof(payload) - 1, 1);
    size_t dn = mqtt_core_build_disconnect(dis, sizeof(dis));
    CHECK(cn > 0 && pn > 0 && dn > 0);
    CHECK(contains(conn, cn, pass));
    CHECK(!contains(pub, pn, pass) && !contains(dis, dn, pass));
}

static void test_host_normalize(void)
{
    char out[64];
    CHECK(mqtt_core_host_normalize("mqtt.example.org", out, sizeof(out)) && strcmp(out, "mqtt.example.org") == 0);
    CHECK(mqtt_core_host_normalize("mqtts://mqtt.example.org", out, sizeof(out)) && strcmp(out, "mqtt.example.org") == 0);
    CHECK(mqtt_core_host_normalize("ssl://h.example:8883/", out, sizeof(out)) && strcmp(out, "h.example") == 0);
    CHECK(mqtt_core_host_normalize("  tls://h.example/path", out, sizeof(out)) && strcmp(out, "h.example") == 0);
    CHECK(mqtt_core_host_normalize("192.168.0.5", out, sizeof(out)) && strcmp(out, "192.168.0.5") == 0);
    CHECK(!mqtt_core_host_normalize("", out, sizeof(out)) && !mqtt_core_host_normalize("mqtts://", out, sizeof(out)));
    CHECK(!mqtt_core_host_normalize(NULL, out, sizeof(out)) && !mqtt_core_host_normalize("h", out, 0));
    CHECK(!mqtt_core_host_normalize("a-very-long-host.example", out, 8));          // не вміщується
}

int main(void)
{
    test_host_normalize();
    test_connect_reference();
    test_connect_rejects_bad_args();
    test_publish_reference();
    test_publish_remaining_length_multibyte();
    test_publish_rejects_bad_args();
    test_disconnect();
    test_connack();
    test_puback();
    test_packet_len();
    test_password_only_in_connect();
    return test_summary();
}
