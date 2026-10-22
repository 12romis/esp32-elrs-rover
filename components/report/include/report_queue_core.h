// Черга звітів з двох слотів і правила спроби передачі (phase8-session-report, ADR-0003, ADR-0004; spec A13, A16, A17).
// Чиста логіка: порядок, витіснення, дозвіл за станом, вихід зі спроби при першій невдачі. Без заголовків ESP-IDF.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"
#include "session_core.h"

#define REPORT_Q_SLOTS 2     // невідправлених звітів не більше двох (spec A17)

typedef struct {
    uint32_t seq;            // номер сесії
    session_end_t reason;    // причина завершення (для показу в `report`)
} report_q_entry_t;

// Слоти впорядковані від найстаршого до найновішого.
typedef struct {
    report_q_entry_t e[REPORT_Q_SLOTS];
    uint8_t count;
} report_q_t;

void report_q_init(report_q_t *q);

// Додає звіт як найновіший. Якщо черга повна, найстарший витісняється: повертає true і пише його номер у *evicted_seq.
// Звіт із номером, що вже є в черзі, ігнорується (повертає false, черга без змін).
bool report_q_push(report_q_t *q, uint32_t seq, session_end_t reason, uint32_t *evicted_seq);

// Відновлення з сховища після перезапуску: записи в довільному порядку; порядок — за зростанням номера, зайві найстаріші
// відкидаються, дублікати ігноруються.
void report_q_restore(report_q_t *q, const report_q_entry_t *entries, size_t n);

uint8_t report_q_count(const report_q_t *q);
// idx 0 — найстарший. false поза межами.
bool report_q_get(const report_q_t *q, uint8_t idx, report_q_entry_t *out);
// Найстарший звіт — наступний до передачі.
bool report_q_next(const report_q_t *q, report_q_entry_t *out);
// Видаляє доставлений звіт. false, якщо такого номера немає.
bool report_q_mark_sent(report_q_t *q, uint32_t seq);

// Наступний номер сесії, що не повторює жодного відомого: max(лічильник у сховищі, найбільший номер у черзі + 1).
uint32_t report_q_next_seq(const report_q_t *q, uint32_t stored_counter);

// ---------- правила спроби ----------

// Результат спроби передачі (рядок результату в `report` і журналі).
typedef enum {
    REPORT_OUT_NONE,           // спроб ще не було
    REPORT_OUT_DELIVERED,      // доставлено (усі звіти, що були в спробі)
    REPORT_OUT_NO_NETWORK,     // Wi-Fi недоступний
    REPORT_OUT_CERT,           // сертифікат сервера не довірений
    REPORT_OUT_NO_ACK,         // немає підтвердження чи відмова сервера
    REPORT_OUT_ABORTED,        // перервано озброєнням
    REPORT_OUT_NO_CONFIG,      // налаштування мережі не задано
    REPORT_OUT_TIMEOUT         // вичерпано ліміт часу спроби
} report_outcome_t;

const char *report_outcome_text(report_outcome_t o);

// Рішення про запуск спроби (автоматичної при вході в DISARMED і за командою `report`).
typedef enum {
    REPORT_GO,                 // запускати
    REPORT_DENY_STATE,         // стан не DISARMED: передача можлива лише в DISARMED
    REPORT_DENY_BUSY,          // спроба вже йде
    REPORT_DENY_EMPTY,         // нічого надсилати: Wi-Fi не вмикати
    REPORT_DENY_NO_CONFIG      // налаштування мережі не задано
} report_gate_t;

bool report_allow_send(app_state_t st);   // лише DISARMED
// Порядок перевірок: стан, спроба вже йде, порожня черга, налаштування.
report_gate_t report_attempt_gate(app_state_t st, bool busy, uint8_t queued, bool config_ok);

#define REPORT_ATTEMPT_LIMIT_MS 30000    // ліміт однієї спроби від її початку до вимкнення Wi-Fi (spec A15)

// Вхід у DISARMED та підсумок сесії (рев'ю phase8, знахідка 1): control закриває сесію на кроці після переходу в DISARMED, тож
// задача report може побачити DISARMED раніше, ніж підсумок потрапив у чергу. Вхід запам'ятовується й розв'язується, коли
// сесія закрита.
// Новий стан очікування: true при переході в DISARMED з іншого стану, скидається при виході; інакше без змін.
bool report_entry_pending(bool pending, app_state_t prev, app_state_t st);
// Чи можна вирішувати про спробу: вхід запам'ятано, стан DISARMED, сесія вже закрита (її підсумок у черзі).
bool report_entry_ready(bool pending, app_state_t st, bool session_active);

// План спроби: знімок черги на момент початку (від найстаршого), перша невдача зупиняє спробу.
typedef struct {
    uint32_t seq[REPORT_Q_SLOTS];
    uint8_t total;
    uint8_t next;              // індекс наступного звіту
    uint8_t delivered;
    bool stopped;
    report_outcome_t outcome;
} report_attempt_t;

void report_attempt_begin(report_attempt_t *a, const report_q_t *q);
// Наступний звіт до передачі; false, якщо спробу закінчено (все передано чи зупинено).
bool report_attempt_next(const report_attempt_t *a, uint32_t *seq);
// Результат передачі поточного звіту. DELIVERED — переходимо до наступного; будь-що інше зупиняє спробу, решта лишається.
void report_attempt_result(report_attempt_t *a, report_outcome_t o);
// Спроба вичерпала ліміт часу (відлік від startMs): зупиняє її з REPORT_OUT_TIMEOUT, якщо ще не зупинена.
bool report_attempt_expired(report_attempt_t *a, uint32_t start_ms, uint32_t now_ms);
// Підсумок спроби: DELIVERED, лише якщо всі звіти доставлено; для порожнього плану — REPORT_OUT_NONE.
report_outcome_t report_attempt_outcome(const report_attempt_t *a);

// ---------- запис звіту у сховищі (ADR-0004) ----------
// Формат блоба слота: версія(1) причина(1) номер(4, LE) довжина(2, LE) документ(довжина) CRC32(4, LE) від усіх попередніх байтів.
// Версіонований, як PID_RECORD_VERSION: невідома версія чи невірний CRC — слот ігнорується.
#define REPORT_RECORD_VERSION 1
#define REPORT_RECORD_HEADER 8
#define REPORT_RECORD_MAX (REPORT_RECORD_HEADER + 1024 + 4)

typedef struct {
    uint32_t seq;
    session_end_t reason;
    const char *json;      // вказує всередину буфера, з якого розібрано (не завершений нулем)
    size_t json_len;
} report_record_t;

// Кодує запис; повертає довжину або 0 (малий буфер, документ порожній чи більший за 1024 байти).
size_t report_record_encode(uint8_t *buf, size_t n, uint32_t seq, session_end_t reason, const char *json, size_t json_len);
// Розбирає запис; false — недійсний (довжина, версія, причина, CRC).
bool report_record_decode(const uint8_t *buf, size_t n, report_record_t *out);

// Метадані: версія(1) номер_наступної_сесії(4, LE) CRC32(4, LE). Блоб 9 байтів.
#define REPORT_META_VERSION 1
#define REPORT_META_SIZE 9
void report_meta_encode(uint8_t out[REPORT_META_SIZE], uint32_t next_seq);
bool report_meta_decode(const uint8_t *buf, size_t n, uint32_t *next_seq);

uint32_t report_crc32(const uint8_t *data, size_t n);
