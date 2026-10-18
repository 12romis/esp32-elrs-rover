// Приймання кадрів CRSF: збирач із потоку байтів, CRC8, розпакування каналів, лічильники.
// Чиста логіка: без заголовків ESP-IDF і FreeRTOS (мітки часу передає виклик).
#include <string.h>

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
