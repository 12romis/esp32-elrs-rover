// Приймання кадрів CRSF: збирач із потоку байтів, CRC8, розпакування каналів, лічильники.
// Чиста логіка: без заголовків ESP-IDF і FreeRTOS (мітки часу передає виклик).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "crsf_calib.h"
#include "crsf_core.h"

void crsf_parser_init(crsf_parser_t *p)
{
    memset(p, 0, sizeof(*p));
}

uint8_t crsf_crc8(const uint8_t *data, size_t n)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (uint8_t)((crc & 0x80) ? ((crc << 1) ^ 0xD5) : (crc << 1));
        }
    }
    return crc;
}

// Розпаковує 16 каналів по 11 біт (молодший біт першим) і обмежує до робочого діапазону.
static void unpack_channels(const uint8_t *payload, uint16_t out[CRSF_CHANNELS])
{
    unsigned bit = 0;
    for (int i = 0; i < CRSF_CHANNELS; i++) {
        unsigned v = 0;
        for (int b = 0; b < 11; b++, bit++) {
            if (payload[bit / 8] & (1u << (bit % 8))) v |= 1u << b;
        }
        if (v < CRSF_RAW_MIN) v = CRSF_RAW_MIN;
        if (v > CRSF_RAW_MAX) v = CRSF_RAW_MAX;
        out[i] = (uint16_t)v;
    }
}

// Відкидає перші k байтів буфера.
static void drop(crsf_parser_t *p, size_t k)
{
    memmove(p->buf, p->buf + k, p->n - k);
    p->n -= k;
}

// Розбирає буфер, доки є що розбирати. Повертає кількість прийнятих кадрів каналів.
static size_t process(crsf_parser_t *p, uint32_t now_ms)
{
    size_t accepted = 0;
    for (;;) {
        if (p->n == 0) break;
        if (p->buf[0] != CRSF_SYNC_BYTE) {
            drop(p, 1);               // шум до початку кадру
            continue;
        }
        if (p->n < 2) break;
        uint8_t len = p->buf[1];
        if (len < CRSF_LEN_MIN || len > CRSF_LEN_MAX) {
            p->snap.cnt.len_fail++;
            drop(p, 1);               // хибний початок: шукаємо наступний 0xC8
            continue;
        }
        size_t total = (size_t)len + 2;
        if (p->n < total) break;      // кадр ще не прийшов цілком
        if (crsf_crc8(p->buf + 2, (size_t)len - 1) != p->buf[total - 1]) {
            p->snap.cnt.crc_fail++;
            drop(p, 1);               // можливо, справжній кадр починається всередині
            continue;
        }
        uint8_t type = p->buf[2];
        if (type == CRSF_TYPE_RC_CHANNELS) {
            if (len != CRSF_LEN_RC) {
                p->snap.cnt.len_fail++;
            } else {
                unpack_channels(p->buf + 3, p->snap.ch);
                p->snap.have_frame = true;
                p->snap.cnt.rc_ok++;
                p->snap.seq = p->snap.cnt.rc_ok;
                p->snap.t_ms = now_ms;
                accepted++;
            }
        } else {
            p->snap.cnt.other++;
        }
        drop(p, total);               // валідний кадр (будь-якого типу) спожито цілком
    }
    return accepted;
}

size_t crsf_feed(crsf_parser_t *p, const uint8_t *data, size_t n, uint32_t now_ms)
{
    size_t accepted = 0;
    for (size_t i = 0; i < n; i++) {
        p->buf[p->n++] = data[i];     // після кожного байта розбираємо, тож буфер не перевищує CRSF_FRAME_MAX
        accepted += process(p, now_ms);
    }
    return accepted;
}

void crsf_note_overflow(crsf_parser_t *p)
{
    p->n = 0;
    p->snap.cnt.overflow++;
}

// ---------- Намір пілота ----------

// Нормалізоване відхилення від центру: (raw − 992) / 819, обмежене до [−1, 1].
static float normalized(uint16_t raw)
{
    float x = ((float)raw - (float)CRSF_RAW_CENTER) / CRSF_HALF_TRAVEL;
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return x;
}

bool crsf_axis_neutral(uint16_t raw)
{
    float x = normalized(raw);
    return x >= -CRSF_DEADBAND && x <= CRSF_DEADBAND;
}

float crsf_axis(uint16_t raw, int sign)
{
    float x = normalized(raw);
    float a = x < 0.0f ? -x : x;
    if (a <= CRSF_DEADBAND) return 0.0f;
    float y = (a - CRSF_DEADBAND) / (1.0f - CRSF_DEADBAND);   // лінійно від нуля на краю зони до 1 на упорі
    if (x < 0.0f) y = -y;
    return sign < 0 ? -y : y;
}

bool crsf_switch_arm(uint16_t raw)
{
    return raw > CRSF_SWITCH_ARM_ABOVE;
}

void crsf_intent(const crsf_snapshot_t *s, crsf_intent_t *out)
{
    if (!s->have_frame) {
        out->throttle = 0.0f;
        out->steer = 0.0f;
        out->throttle_neutral = true;
        out->steer_neutral = true;
        out->sw = CRSF_SW_DISARM;
        return;
    }
    out->throttle = crsf_axis(s->ch[CRSF_IDX_THROTTLE], CRSF_THROTTLE_SIGN);
    out->steer = crsf_axis(s->ch[CRSF_IDX_STEER], CRSF_STEER_SIGN);
    out->throttle_neutral = crsf_axis_neutral(s->ch[CRSF_IDX_THROTTLE]);
    out->steer_neutral = crsf_axis_neutral(s->ch[CRSF_IDX_STEER]);
    out->sw = crsf_switch_arm(s->ch[CRSF_IDX_ARM]) ? CRSF_SW_ARM : CRSF_SW_DISARM;
}

bool crsf_edge(crsf_switch_t *base, crsf_switch_t now)
{
    bool edge = (*base == CRSF_SW_DISARM && now == CRSF_SW_ARM);
    *base = now;
    return edge;
}

// ---------- Рядки status ----------

// Дописує форматований рядок у buf (розмір n) після позиції *pos; переповнення обрізає, не ламаючи рядок.
static void put(char *buf, size_t n, size_t *pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static void put(char *buf, size_t n, size_t *pos, const char *fmt, ...)
{
    if (*pos >= n - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + *pos, n - *pos, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    *pos = (size_t)k >= n - *pos ? n - 1 : *pos + (size_t)k;
}

size_t crsf_fmt_status(char *buf, size_t n, const crsf_status_view_t *v)
{
    if (n == 0) return 0;
    buf[0] = '\0';
    size_t pos = 0;
    const char *src = v->source == CRSF_SRC_BENCH ? "стенд" : (v->source == CRSF_SRC_REMOTE ? "пульт" : "немає");
    const char *link = v->link == CRSF_LINK_NEVER ? "не було від старту" : (v->link == CRSF_LINK_ALIVE ? "живий" : "втрачено");
    put(buf, n, &pos, "керування: %s\n", src);
    if (v->link == CRSF_LINK_NEVER) {
        put(buf, n, &pos, "звʼязок: %s; від останнього кадру: —\n", link);
    } else {
        put(buf, n, &pos, "звʼязок: %s; від останнього кадру: %u мс\n", link, (unsigned)v->since_last_ms);
    }
    put(buf, n, &pos, "кадри: каналів %u, CRC-помилок %u, довжина %u, інші %u, переповнення %u\n",
        (unsigned)v->snap.cnt.rc_ok, (unsigned)v->snap.cnt.crc_fail, (unsigned)v->snap.cnt.len_fail,
        (unsigned)v->snap.cnt.other, (unsigned)v->snap.cnt.overflow);
    if (v->snap.have_frame) {
        put(buf, n, &pos, "канали: CH1 %u, CH2 %u, CH5 %u\n", (unsigned)v->snap.ch[CRSF_IDX_STEER],
            (unsigned)v->snap.ch[CRSF_IDX_THROTTLE], (unsigned)v->snap.ch[CRSF_IDX_ARM]);
    } else {
        put(buf, n, &pos, "канали: CH1 —, CH2 —, CH5 —\n");
    }
    const char *sw = v->intent.sw == CRSF_SW_ARM ? "ARM" : (v->intent.sw == CRSF_SW_DISARM ? "DISARM" : "невідомо");
    put(buf, n, &pos, "рух: тяга %+.2f, поворот %+.2f; тумблер: %s\n", (double)v->intent.throttle,
        (double)v->intent.steer, sw);
    if (v->arm_block != NULL) {
        put(buf, n, &pos, "озброєння з пульта заблоковано: %s\n", v->arm_block);
    } else {
        put(buf, n, &pos, "озброєння з пульта: можна\n");
    }
    if (v->failsafe_cause != NULL) {
        put(buf, n, &pos, "причина FAILSAFE: %s\n", v->failsafe_cause);
    }
    return pos;
}
