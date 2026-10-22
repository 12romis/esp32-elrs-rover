// Черга звітів з двох слотів і правила спроби передачі (phase8-session-report): див. report_queue_core.h.
// Без заголовків ESP-IDF.
#include "report_queue_core.h"

#include <string.h>

void report_q_init(report_q_t *q)
{
    memset(q, 0, sizeof(*q));
}

static bool has_seq(const report_q_t *q, uint32_t seq)
{
    for (uint8_t i = 0; i < q->count; i++) {
        if (q->e[i].seq == seq) {
            return true;
        }
    }
    return false;
}

bool report_q_push(report_q_t *q, uint32_t seq, session_end_t reason, uint32_t *evicted_seq)
{
    if (q == NULL || has_seq(q, seq)) {
        return false;
    }
    bool evicted = false;
    if (q->count == REPORT_Q_SLOTS) {
        if (evicted_seq != NULL) {
            *evicted_seq = q->e[0].seq;
        }
        for (uint8_t i = 1; i < REPORT_Q_SLOTS; i++) {
            q->e[i - 1] = q->e[i];
        }
        q->count--;
        evicted = true;
    }
    q->e[q->count].seq = seq;
    q->e[q->count].reason = reason;
    q->count++;
    return evicted;
}

void report_q_restore(report_q_t *q, const report_q_entry_t *entries, size_t n)
{
    report_q_init(q);
    if (entries == NULL) {
        return;
    }
    // Вставка за зростанням номера; push витісняє найстарший, тож лишаються найновіші.
    report_q_entry_t sorted[REPORT_Q_SLOTS + 8];
    size_t m = 0;
    for (size_t i = 0; i < n && m < sizeof(sorted) / sizeof(sorted[0]); i++) {
        bool dup = false;
        for (size_t j = 0; j < m; j++) {
            dup = dup || sorted[j].seq == entries[i].seq;
        }
        if (dup) {
            continue;
        }
        size_t pos = m;
        while (pos > 0 && sorted[pos - 1].seq > entries[i].seq) {
            sorted[pos] = sorted[pos - 1];
            pos--;
        }
        sorted[pos] = entries[i];
        m++;
    }
    for (size_t i = 0; i < m; i++) {
        report_q_push(q, sorted[i].seq, sorted[i].reason, NULL);
    }
}

uint8_t report_q_count(const report_q_t *q)
{
    return q ? q->count : 0;
}

bool report_q_get(const report_q_t *q, uint8_t idx, report_q_entry_t *out)
{
    if (q == NULL || out == NULL || idx >= q->count) {
        return false;
    }
    *out = q->e[idx];
    return true;
}

bool report_q_next(const report_q_t *q, report_q_entry_t *out)
{
    return report_q_get(q, 0, out);
}

bool report_q_mark_sent(report_q_t *q, uint32_t seq)
{
    if (q == NULL) {
        return false;
    }
    for (uint8_t i = 0; i < q->count; i++) {
        if (q->e[i].seq == seq) {
            for (uint8_t j = i + 1; j < q->count; j++) {
                q->e[j - 1] = q->e[j];
            }
            q->count--;
            return true;
        }
    }
    return false;
}

uint32_t report_q_next_seq(const report_q_t *q, uint32_t stored_counter)
{
    uint32_t next = stored_counter;
    if (q != NULL) {
        for (uint8_t i = 0; i < q->count; i++) {
            if (q->e[i].seq >= next) {
                next = q->e[i].seq + 1;
            }
        }
    }
    return next == 0 ? 1 : next;   // нумерація з одиниці: нуль у звіті не зустрічається
}

const char *report_outcome_text(report_outcome_t o)
{
    switch (o) {
    case REPORT_OUT_NONE:
        return "спроб не було";
    case REPORT_OUT_DELIVERED:
        return "доставлено";
    case REPORT_OUT_NO_NETWORK:
        return "немає мережі";
    case REPORT_OUT_CERT:
        return "сертифікат сервера не довірений";
    case REPORT_OUT_NO_ACK:
        return "немає підтвердження від сервера";
    case REPORT_OUT_ABORTED:
        return "перервано озброєнням";
    case REPORT_OUT_NO_CONFIG:
        return "налаштування мережі не задано";
    case REPORT_OUT_TIMEOUT:
        return "вичерпано ліміт часу спроби";
    }
    return "невідомий результат";
}

bool report_allow_send(app_state_t st)
{
    return st == APP_STATE_DISARMED;
}

report_gate_t report_attempt_gate(app_state_t st, bool busy, uint8_t queued, bool config_ok)
{
    if (!report_allow_send(st)) {
        return REPORT_DENY_STATE;
    }
    if (busy) {
        return REPORT_DENY_BUSY;
    }
    if (queued == 0) {
        return REPORT_DENY_EMPTY;
    }
    if (!config_ok) {
        return REPORT_DENY_NO_CONFIG;
    }
    return REPORT_GO;
}

bool report_entry_pending(bool pending, app_state_t prev, app_state_t st)
{
    if (st != APP_STATE_DISARMED) {
        return false;
    }
    return prev != APP_STATE_DISARMED ? true : pending;
}

bool report_entry_ready(bool pending, app_state_t st, bool session_active)
{
    return pending && st == APP_STATE_DISARMED && !session_active;
}

void report_attempt_begin(report_attempt_t *a, const report_q_t *q)
{
    memset(a, 0, sizeof(*a));
    a->outcome = REPORT_OUT_NONE;
    for (uint8_t i = 0; q != NULL && i < q->count && i < REPORT_Q_SLOTS; i++) {
        a->seq[a->total++] = q->e[i].seq;
    }
}

bool report_attempt_next(const report_attempt_t *a, uint32_t *seq)
{
    if (a == NULL || seq == NULL || a->stopped || a->next >= a->total) {
        return false;
    }
    *seq = a->seq[a->next];
    return true;
}

void report_attempt_result(report_attempt_t *a, report_outcome_t o)
{
    if (a == NULL || a->stopped || a->next >= a->total) {
        return;
    }
    if (o == REPORT_OUT_DELIVERED) {
        a->delivered++;
        a->next++;
        if (a->next >= a->total) {
            a->outcome = REPORT_OUT_DELIVERED;
        }
        return;
    }
    a->stopped = true;
    a->outcome = o;
}

bool report_attempt_expired(report_attempt_t *a, uint32_t start_ms, uint32_t now_ms)
{
    if (a == NULL || a->stopped || a->next >= a->total) {
        return false;
    }
    if ((uint32_t)(now_ms - start_ms) < REPORT_ATTEMPT_LIMIT_MS) {
        return false;
    }
    a->stopped = true;
    a->outcome = REPORT_OUT_TIMEOUT;
    return true;
}

report_outcome_t report_attempt_outcome(const report_attempt_t *a)
{
    return a ? a->outcome : REPORT_OUT_NONE;
}

// ---------- запис звіту у сховищі ----------

uint32_t report_crc32(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; data != NULL && i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

static void put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t report_record_encode(uint8_t *buf, size_t n, uint32_t seq, session_end_t reason, const char *json, size_t json_len)
{
    if (buf == NULL || json == NULL || json_len == 0 || json_len > 1024) {
        return 0;
    }
    size_t total = REPORT_RECORD_HEADER + json_len + 4;
    if (n < total) {
        return 0;
    }
    buf[0] = REPORT_RECORD_VERSION;
    buf[1] = (uint8_t)reason;
    put_le32(buf + 2, seq);
    buf[6] = (uint8_t)(json_len & 0xFF);
    buf[7] = (uint8_t)(json_len >> 8);
    memcpy(buf + REPORT_RECORD_HEADER, json, json_len);
    put_le32(buf + REPORT_RECORD_HEADER + json_len, report_crc32(buf, REPORT_RECORD_HEADER + json_len));
    return total;
}

bool report_record_decode(const uint8_t *buf, size_t n, report_record_t *out)
{
    if (buf == NULL || out == NULL || n < REPORT_RECORD_HEADER + 1 + 4 || buf[0] != REPORT_RECORD_VERSION ||
        buf[1] > (uint8_t)SESSION_END_VOLTAGE) {
        return false;
    }
    size_t len = (size_t)buf[6] | ((size_t)buf[7] << 8);
    if (len == 0 || len > 1024 || n != REPORT_RECORD_HEADER + len + 4) {
        return false;
    }
    if (get_le32(buf + REPORT_RECORD_HEADER + len) != report_crc32(buf, REPORT_RECORD_HEADER + len)) {
        return false;
    }
    out->seq = get_le32(buf + 2);
    out->reason = (session_end_t)buf[1];
    out->json = (const char *)buf + REPORT_RECORD_HEADER;
    out->json_len = len;
    return true;
}

void report_meta_encode(uint8_t out[REPORT_META_SIZE], uint32_t next_seq)
{
    out[0] = REPORT_META_VERSION;
    put_le32(out + 1, next_seq);
    put_le32(out + 5, report_crc32(out, 5));
}

bool report_meta_decode(const uint8_t *buf, size_t n, uint32_t *next_seq)
{
    if (buf == NULL || next_seq == NULL || n != REPORT_META_SIZE || buf[0] != REPORT_META_VERSION ||
        get_le32(buf + 5) != report_crc32(buf, 5)) {
        return false;
    }
    *next_seq = get_le32(buf + 1);
    return true;
}
