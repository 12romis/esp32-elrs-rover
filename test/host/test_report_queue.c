// Хостові тести report_queue_core (phase8-session-report T9; spec A13, A16, A17; AC-15, AC-16, AC-17): черга з двох слотів,
// порядок передачі, вихід зі спроби при першій невдачі, дозвіл за станом.
#include <string.h>

#include "report_queue_core.h"
#include "test_support.h"

static const app_state_t ALL[APP_STATE_COUNT] = {APP_STATE_BOOT, APP_STATE_SELFTEST, APP_STATE_DISARMED, APP_STATE_ARMED,
                                                 APP_STATE_OBSTACLE_HOLD, APP_STATE_FAILSAFE, APP_STATE_FAULT};

static void test_push_order_and_eviction(void)
{
    report_q_t q;
    report_q_init(&q);
    uint32_t ev = 0;
    CHECK(report_q_count(&q) == 0);
    report_q_entry_t e;
    CHECK(!report_q_next(&q, &e));
    CHECK(!report_q_push(&q, 5, SESSION_END_DISARM, &ev));
    CHECK(!report_q_push(&q, 6, SESSION_END_LINK_LOST, &ev));
    CHECK(report_q_count(&q) == 2);
    CHECK(report_q_next(&q, &e) && e.seq == 5 && e.reason == SESSION_END_DISARM);
    // AC-15: третій витісняє найстарший, лишаються два новіші
    CHECK(report_q_push(&q, 7, SESSION_END_VOLTAGE, &ev) && ev == 5);
    CHECK(report_q_count(&q) == 2);
    CHECK(report_q_get(&q, 0, &e) && e.seq == 6 && report_q_get(&q, 1, &e) && e.seq == 7 && e.reason == SESSION_END_VOLTAGE);
    CHECK(!report_q_get(&q, 2, &e));
    // ще один: невідправлених ніколи не більше двох
    CHECK(report_q_push(&q, 8, SESSION_END_DISARM, &ev) && ev == 6 && report_q_count(&q) == 2);
    // дубль номера ігнорується
    CHECK(!report_q_push(&q, 8, SESSION_END_DISARM, &ev) && report_q_count(&q) == 2);
    CHECK(report_q_push(&q, 9, SESSION_END_DISARM, NULL));                 // NULL для номера витісненого допустимий
    CHECK(report_q_get(&q, 0, &e) && e.seq == 8);
}

static void test_mark_sent(void)
{
    report_q_t q;
    report_q_init(&q);
    uint32_t ev;
    report_q_push(&q, 1, SESSION_END_DISARM, &ev);
    report_q_push(&q, 2, SESSION_END_DISARM, &ev);
    CHECK(!report_q_mark_sent(&q, 99));
    CHECK(report_q_mark_sent(&q, 1) && report_q_count(&q) == 1);
    report_q_entry_t e;
    CHECK(report_q_next(&q, &e) && e.seq == 2);
    CHECK(report_q_mark_sent(&q, 2) && report_q_count(&q) == 0);
    CHECK(!report_q_mark_sent(&q, 2));
    // видалення новішого зберігає порядок решти
    report_q_push(&q, 3, SESSION_END_DISARM, &ev);
    report_q_push(&q, 4, SESSION_END_DISARM, &ev);
    CHECK(report_q_mark_sent(&q, 4) && report_q_next(&q, &e) && e.seq == 3);
}

static void test_restore_and_next_seq(void)
{
    report_q_t q;
    report_q_init(&q);
    // зі сховища у довільному порядку, з дублем
    report_q_entry_t in[] = {{12, SESSION_END_VOLTAGE}, {10, SESSION_END_DISARM}, {12, SESSION_END_VOLTAGE}};
    report_q_restore(&q, in, 3);
    report_q_entry_t e;
    CHECK(report_q_count(&q) == 2 && report_q_get(&q, 0, &e) && e.seq == 10 && report_q_get(&q, 1, &e) && e.seq == 12);
    // більше двох: лишаються два найновіші
    report_q_entry_t many[] = {{3, SESSION_END_DISARM}, {1, SESSION_END_DISARM}, {2, SESSION_END_DISARM}};
    report_q_restore(&q, many, 3);
    CHECK(report_q_count(&q) == 2 && report_q_get(&q, 0, &e) && e.seq == 2 && report_q_get(&q, 1, &e) && e.seq == 3);
    report_q_restore(&q, NULL, 0);
    CHECK(report_q_count(&q) == 0);
    // номер сесії: максимум із лічильника та слотів плюс 1
    report_q_restore(&q, in, 2);
    CHECK(report_q_next_seq(&q, 5) == 13);           // лічильник відстав — слоти перемагають
    CHECK(report_q_next_seq(&q, 40) == 40);          // лічильник попереду
    report_q_init(&q);
    CHECK(report_q_next_seq(&q, 0) == 1 || report_q_next_seq(&q, 0) == 0);
    CHECK(report_q_next_seq(&q, 7) == 7);
}

static void test_allow_send_per_state(void)
{
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        CHECK(report_allow_send(ALL[i]) == (ALL[i] == APP_STATE_DISARMED));
    }
}

static void test_gate(void)
{
    // AC-17: ARMED, OBSTACLE_HOLD, FAILSAFE (і решта) — стан показується, передача не запускається
    for (int i = 0; i < APP_STATE_COUNT; i++) {
        report_gate_t g = report_attempt_gate(ALL[i], false, 2, true);
        CHECK(g == (ALL[i] == APP_STATE_DISARMED ? REPORT_GO : REPORT_DENY_STATE));
    }
    CHECK(report_attempt_gate(APP_STATE_DISARMED, true, 2, true) == REPORT_DENY_BUSY);
    CHECK(report_attempt_gate(APP_STATE_DISARMED, false, 0, true) == REPORT_DENY_EMPTY);
    CHECK(report_attempt_gate(APP_STATE_DISARMED, false, 1, false) == REPORT_DENY_NO_CONFIG);
    // порядок перевірок: стан сильніший за решту; зайнятість — за порожнечу; порожня — за відсутні налаштування
    CHECK(report_attempt_gate(APP_STATE_ARMED, true, 0, false) == REPORT_DENY_STATE);
    CHECK(report_attempt_gate(APP_STATE_DISARMED, true, 0, false) == REPORT_DENY_BUSY);
    CHECK(report_attempt_gate(APP_STATE_DISARMED, false, 0, false) == REPORT_DENY_EMPTY);
}

static void test_attempt_all_delivered(void)
{
    // AC-16: обидва звіти в порядку створення
    report_q_t q;
    report_q_init(&q);
    uint32_t ev;
    report_q_push(&q, 20, SESSION_END_DISARM, &ev);
    report_q_push(&q, 21, SESSION_END_LINK_LOST, &ev);
    report_attempt_t a;
    report_attempt_begin(&a, &q);
    uint32_t seq = 0;
    CHECK(report_attempt_next(&a, &seq) && seq == 20);
    report_attempt_result(&a, REPORT_OUT_DELIVERED);
    CHECK(report_attempt_next(&a, &seq) && seq == 21);
    report_attempt_result(&a, REPORT_OUT_DELIVERED);
    CHECK(!report_attempt_next(&a, &seq));
    CHECK(report_attempt_outcome(&a) == REPORT_OUT_DELIVERED && a.delivered == 2);
}

static void test_attempt_stops_on_first_failure(void)
{
    report_q_t q;
    report_q_init(&q);
    uint32_t ev;
    report_q_push(&q, 20, SESSION_END_DISARM, &ev);
    report_q_push(&q, 21, SESSION_END_DISARM, &ev);
    report_attempt_t a;
    report_attempt_begin(&a, &q);
    uint32_t seq = 0;
    CHECK(report_attempt_next(&a, &seq) && seq == 20);
    report_attempt_result(&a, REPORT_OUT_NO_ACK);
    CHECK(!report_attempt_next(&a, &seq));                          // другий не надсилається
    CHECK(report_attempt_outcome(&a) == REPORT_OUT_NO_ACK && a.delivered == 0);
    CHECK(report_q_count(&q) == 2);                                  // черга не змінена спробою
    // перший доставлено, другий ні: решта лишається, підсумок — причина невдачі
    report_attempt_begin(&a, &q);
    report_attempt_next(&a, &seq);
    report_attempt_result(&a, REPORT_OUT_DELIVERED);
    report_q_mark_sent(&q, seq);
    CHECK(report_attempt_next(&a, &seq) && seq == 21);
    report_attempt_result(&a, REPORT_OUT_CERT);
    CHECK(!report_attempt_next(&a, &seq) && report_attempt_outcome(&a) == REPORT_OUT_CERT && a.delivered == 1);
    report_q_entry_t e;
    CHECK(report_q_count(&q) == 1 && report_q_next(&q, &e) && e.seq == 21);
    // результат після зупинки спроби нічого не змінює
    report_attempt_result(&a, REPORT_OUT_DELIVERED);
    CHECK(report_attempt_outcome(&a) == REPORT_OUT_CERT);
}

static void test_attempt_empty_and_timeout(void)
{
    report_q_t q;
    report_q_init(&q);
    report_attempt_t a;
    report_attempt_begin(&a, &q);
    uint32_t seq;
    CHECK(!report_attempt_next(&a, &seq) && report_attempt_outcome(&a) == REPORT_OUT_NONE);
    uint32_t ev;
    report_q_push(&q, 1, SESSION_END_DISARM, &ev);
    report_attempt_begin(&a, &q);
    CHECK(!report_attempt_expired(&a, 1000, 1000 + REPORT_ATTEMPT_LIMIT_MS - 1));
    CHECK(report_attempt_expired(&a, 1000, 1000 + REPORT_ATTEMPT_LIMIT_MS));
    CHECK(!report_attempt_next(&a, &seq) && report_attempt_outcome(&a) == REPORT_OUT_TIMEOUT);
    // переповнення годинника мс
    report_attempt_begin(&a, &q);
    CHECK(report_attempt_expired(&a, 0xFFFFF000u, 0xFFFFF000u + REPORT_ATTEMPT_LIMIT_MS));
    // зупинена спроба вдруге не «спливає»
    CHECK(!report_attempt_expired(&a, 0, 100000));
    // доставлену спробу тайм-аут не чіпає
    report_attempt_begin(&a, &q);
    report_attempt_next(&a, &seq);
    report_attempt_result(&a, REPORT_OUT_DELIVERED);
    CHECK(!report_attempt_expired(&a, 0, 100000) && report_attempt_outcome(&a) == REPORT_OUT_DELIVERED);
}

static void test_outcome_text(void)
{
    for (int o = REPORT_OUT_NONE; o <= REPORT_OUT_TIMEOUT; o++) {
        CHECK(strlen(report_outcome_text((report_outcome_t)o)) > 0);
    }
    CHECK(strstr(report_outcome_text(REPORT_OUT_CERT), "сертифікат") != NULL);
    CHECK(strstr(report_outcome_text(REPORT_OUT_ABORTED), "озброєн") != NULL);
}

static void test_record_roundtrip(void)
{
    uint8_t buf[REPORT_RECORD_MAX];
    const char *doc = "{\"seq\":42}";
    size_t n = report_record_encode(buf, sizeof(buf), 42, SESSION_END_LINK_LOST, doc, strlen(doc));
    CHECK(n == REPORT_RECORD_HEADER + strlen(doc) + 4);
    report_record_t r;
    CHECK(report_record_decode(buf, n, &r));
    CHECK(r.seq == 42 && r.reason == SESSION_END_LINK_LOST && r.json_len == strlen(doc) && memcmp(r.json, doc, r.json_len) == 0);
    CHECK(buf[0] == REPORT_RECORD_VERSION);
    // найбільший документ
    char big[1024];
    memset(big, 'x', sizeof(big));
    n = report_record_encode(buf, sizeof(buf), 0xFFFFFFFFu, SESSION_END_VOLTAGE, big, sizeof(big));
    CHECK(n == REPORT_RECORD_MAX && report_record_decode(buf, n, &r) && r.seq == 0xFFFFFFFFu && r.json_len == 1024);
    // відмови кодера
    CHECK(report_record_encode(buf, sizeof(buf), 1, SESSION_END_DISARM, doc, 0) == 0);
    CHECK(report_record_encode(buf, sizeof(buf), 1, SESSION_END_DISARM, big, sizeof(big) + 1) == 0);
    CHECK(report_record_encode(buf, 10, 1, SESSION_END_DISARM, doc, strlen(doc)) == 0);
    CHECK(report_record_encode(NULL, 0, 1, SESSION_END_DISARM, doc, strlen(doc)) == 0);
}

static void test_record_rejects_damage(void)
{
    uint8_t buf[REPORT_RECORD_MAX], bad[REPORT_RECORD_MAX];
    const char *doc = "{\"a\":1}";
    size_t n = report_record_encode(buf, sizeof(buf), 7, SESSION_END_DISARM, doc, strlen(doc));
    report_record_t r;
    CHECK(report_record_decode(buf, n, &r));
    // будь-який змінений байт ламає CRC (чи версію)
    for (size_t i = 0; i < n; i++) {
        memcpy(bad, buf, n);
        bad[i] ^= 0x40;
        CHECK(!report_record_decode(bad, n, &r));
    }
    // обрізаний, задовгий, порожній
    CHECK(!report_record_decode(buf, n - 1, &r) && !report_record_decode(buf, REPORT_RECORD_HEADER, &r));
    CHECK(!report_record_decode(buf, 0, &r) && !report_record_decode(NULL, 5, &r) && !report_record_decode(buf, n, NULL));
    // невідома версія з правильним CRC
    memcpy(bad, buf, n);
    bad[0] = 99;
    uint32_t crc = report_crc32(bad, n - 4);
    for (int i = 0; i < 4; i++) bad[n - 4 + i] = (uint8_t)(crc >> (8 * i));
    CHECK(!report_record_decode(bad, n, &r));
    // довжина в заголовку більша за блоб
    memcpy(bad, buf, n);
    bad[6] = 0xFF;
    bad[7] = 0x0F;
    CHECK(!report_record_decode(bad, n, &r));
    // невідома причина з правильним CRC
    memcpy(bad, buf, n);
    bad[1] = 77;
    crc = report_crc32(bad, n - 4);
    for (int i = 0; i < 4; i++) bad[n - 4 + i] = (uint8_t)(crc >> (8 * i));
    CHECK(!report_record_decode(bad, n, &r));
}

static void test_crc32_reference(void)
{
    CHECK(report_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);        // еталон CRC-32/IEEE
    CHECK(report_crc32(NULL, 0) == 0x00000000u);
}

static void test_meta(void)
{
    uint8_t m[REPORT_META_SIZE];
    uint32_t next = 0;
    report_meta_encode(m, 0x01020304u);
    CHECK(report_meta_decode(m, sizeof(m), &next) && next == 0x01020304u);
    uint8_t bad[REPORT_META_SIZE];
    for (size_t i = 0; i < sizeof(m); i++) {
        memcpy(bad, m, sizeof(m));
        bad[i] ^= 0x01;
        CHECK(!report_meta_decode(bad, sizeof(bad), &next));
    }
    CHECK(!report_meta_decode(m, sizeof(m) - 1, &next) && !report_meta_decode(NULL, 9, &next) && !report_meta_decode(m, 9, NULL));
}

// Рев'ю знахідка 1: підсумок сесії приходить у чергу трохи пізніше за перехід у DISARMED. Вхід у DISARMED запам'ятовується
// і розв'язується, лише коли сесія закрита (підсумок уже в черзі).
static void test_entry_pending_waits_for_session_close(void)
{
    bool pending = false;
    app_state_t prev = APP_STATE_ARMED;
    // крок N: стан уже DISARMED, а сесія ще не закрита → вхід запам'ятано, але спроби ще немає
    pending = report_entry_pending(pending, prev, APP_STATE_DISARMED);
    prev = APP_STATE_DISARMED;
    CHECK(pending && !report_entry_ready(pending, APP_STATE_DISARMED, true));
    // сесія закрилась, підсумок збережено → тепер можна вирішувати (у тому ж DISARMED)
    pending = report_entry_pending(pending, prev, APP_STATE_DISARMED);
    CHECK(pending && report_entry_ready(pending, APP_STATE_DISARMED, false));
    // після рішення прапорець скидає виклик (HAL), повторного входу без нового переходу немає
    pending = false;
    pending = report_entry_pending(pending, prev, APP_STATE_DISARMED);
    CHECK(!pending && !report_entry_ready(pending, APP_STATE_DISARMED, false));
    // вихід зі стану скасовує очікування: озброєння до рішення
    pending = report_entry_pending(true, APP_STATE_DISARMED, APP_STATE_ARMED);
    CHECK(!pending && !report_entry_ready(true, APP_STATE_ARMED, false));
    // перший вхід після старту плати (SELFTEST → DISARMED) без сесії: готово одразу
    pending = report_entry_pending(false, APP_STATE_SELFTEST, APP_STATE_DISARMED);
    CHECK(pending && report_entry_ready(pending, APP_STATE_DISARMED, false));
    // FAILSAFE → DISARMED
    CHECK(report_entry_pending(false, APP_STATE_FAILSAFE, APP_STATE_DISARMED));
}

int main(void)
{
    test_entry_pending_waits_for_session_close();
    test_record_roundtrip();
    test_record_rejects_damage();
    test_crc32_reference();
    test_meta();
    test_push_order_and_eviction();
    test_mark_sent();
    test_restore_and_next_seq();
    test_allow_send_per_state();
    test_gate();
    test_attempt_all_delivered();
    test_attempt_stops_on_first_failure();
    test_attempt_empty_and_timeout();
    test_outcome_text();
    return test_summary();
}
