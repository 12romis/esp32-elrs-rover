// Чиста логіка далекомірів (phase6-tof, ADR-0001, ADR-0003): без заголовків ESP-IDF, перевіряється на хості.
#include "tof_core.h"

#include <stdio.h>
#include <string.h>

#include "tof_calib.h"

static bool timed(tof_state_t s)
{
    return s == TOF_S_NOT_READY || s == TOF_S_OK || s == TOF_S_OUT;
}

static bool working(tof_state_t s)
{
    return s == TOF_S_OK || s == TOF_S_OUT;
}

tof_sector_t tof_sector_of(tof_id_t id)
{
    return id == TOF_REAR ? TOF_SECTOR_REAR : TOF_SECTOR_FRONT;
}

void tof_core_init(tof_core_t *c)
{
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < TOF_SENSORS; i++) {
        c->s[i].state = TOF_S_INIT;
    }
}

void tof_core_start(tof_core_t *c, tof_id_t id, uint32_t now_ms, tof_events_t *ev)
{
    tof_sensor_t *s = &c->s[id];
    bool was_failed = s->state == TOF_S_FAILED;
    memset(s, 0, sizeof(*s));
    s->state = TOF_S_NOT_READY;
    s->last_valid_ms = now_ms;
    if (was_failed && ev != NULL) {
        ev->restored_now |= (uint8_t)(1u << id);
    }
}

void tof_core_mark_failed(tof_core_t *c, tof_id_t id, tof_events_t *ev)
{
    tof_sensor_t *s = &c->s[id];
    if (s->state == TOF_S_FAILED) {
        return;
    }
    s->state = TOF_S_FAILED;
    s->count = 0;
    s->dirty = false;
    if (ev != NULL) {
        ev->failed_now |= (uint8_t)(1u << id);
    }
}

static uint16_t median3(const uint16_t v[TOF_MEDIAN_N])
{
    uint16_t a = v[0], b = v[1], d = v[2];
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > d) { uint16_t t = b; b = d; d = t; }
    if (a > b) { uint16_t t = a; a = b; b = t; }
    return b;
}

void tof_core_sample(tof_core_t *c, tof_id_t id, uint16_t mm, uint32_t now_ms)
{
    tof_sensor_t *s = &c->s[id];
    if (!timed(s->state)) {
        return;   // INIT і FAILED відліків не приймають
    }
    s->raw[s->head] = mm;
    s->head = (uint8_t)((s->head + 1) % TOF_MEDIAN_N);
    if (s->count < TOF_MEDIAN_N) {
        s->count++;
    }
    s->last_valid_ms = now_ms;
    s->dirty = true;
    tof_sector_state_t *sec = &c->sec[tof_sector_of(id)];
    if (mm < TOF_OBSTACLE_MM && !sec->below) {   // перший сирий відлік менше порога — початок відліку реакції
        sec->below = true;
        sec->below_ms = now_ms;
    }
    if (s->count < TOF_MEDIAN_N) {
        s->state = TOF_S_NOT_READY;
        return;
    }
    uint16_t m = median3(s->raw);
    if (m >= TOF_OUT_OF_RANGE_MM) {
        s->state = TOF_S_OUT;
        s->mm = TOF_OUT_OF_RANGE_MM;
    } else {
        s->state = TOF_S_OK;
        s->mm = m;
    }
}

// Відстань, найближчий датчик і ознака «охороняється» сектору за станом датчиків.
static void sector_calc(const tof_core_t *c, tof_sector_t sec, bool *guarded, uint16_t *mm, int8_t *nearest)
{
    *guarded = false;
    *mm = TOF_OUT_OF_RANGE_MM;
    *nearest = -1;
    for (int i = 0; i < TOF_SENSORS; i++) {
        if (tof_sector_of((tof_id_t)i) != sec || !working(c->s[i].state)) {
            continue;
        }
        *guarded = true;
        if (c->s[i].state == TOF_S_OK && c->s[i].mm < *mm) {
            *mm = c->s[i].mm;
            *nearest = (int8_t)i;
        }
    }
}

// Чи лишився в кільцях датчиків сектору сирий відлік менше порога.
static bool sector_has_low_raw(const tof_core_t *c, tof_sector_t sec)
{
    for (int i = 0; i < TOF_SENSORS; i++) {
        if (tof_sector_of((tof_id_t)i) != sec || !timed(c->s[i].state)) {
            continue;
        }
        for (int j = 0; j < c->s[i].count; j++) {
            if (c->s[i].raw[j] < TOF_OBSTACLE_MM) {
                return true;
            }
        }
    }
    return false;
}

void tof_core_tick(tof_core_t *c, uint32_t now_ms, tof_events_t *ev)
{
    for (int i = 0; i < TOF_SENSORS; i++) {
        if (timed(c->s[i].state) && (uint32_t)(now_ms - c->s[i].last_valid_ms) >= TOF_FAIL_MS) {
            tof_core_mark_failed(c, (tof_id_t)i, ev);
        }
    }
    for (int k = 0; k < TOF_SECTORS; k++) {
        bool guarded, updated = false;
        uint16_t mm;
        int8_t nearest;
        sector_calc(c, (tof_sector_t)k, &guarded, &mm, &nearest);
        for (int i = 0; i < TOF_SENSORS; i++) {
            if (tof_sector_of((tof_id_t)i) == (tof_sector_t)k && c->s[i].dirty) {
                updated = true;
            }
        }
        tof_sector_state_t *st = &c->sec[k];
        if (st->below && !st->occupied && !sector_has_low_raw(c, (tof_sector_t)k)) {
            st->below = false;   // викид витіснено з кілець, перешкоди не було
        }
        if (!guarded) {
            st->occupied = false;
            st->free_count = 0;
        } else if (mm < TOF_OBSTACLE_MM) {
            st->occupied = true;
            st->free_count = 0;
        } else if (st->occupied && updated) {
            if (++st->free_count >= TOF_RELEASE_COUNT) {
                st->occupied = false;
                st->free_count = 0;
            }
        }
    }
    for (int i = 0; i < TOF_SENSORS; i++) {
        c->s[i].dirty = false;
    }
}

void tof_core_resume(tof_core_t *c, uint32_t now_ms)
{
    for (int i = 0; i < TOF_SENSORS; i++) {
        if (timed(c->s[i].state)) {
            c->s[i].last_valid_ms = now_ms;
        }
    }
}

void tof_core_view(const tof_core_t *c, tof_view_t *v)
{
    memset(v, 0, sizeof(*v));
    for (int i = 0; i < TOF_SENSORS; i++) {
        v->sensor[i].state = c->s[i].state;
        v->sensor[i].mm = c->s[i].state == TOF_S_OK ? c->s[i].mm : 0;
    }
    for (int k = 0; k < TOF_SECTORS; k++) {
        bool guarded;
        sector_calc(c, (tof_sector_t)k, &guarded, &v->sector[k].mm, &v->sector[k].nearest);
        v->sector[k].guarded = guarded;
        v->sector[k].occupied = guarded && c->sec[k].occupied;
        v->sector[k].has_below = c->sec[k].below;
        v->sector[k].below_ms = c->sec[k].below_ms;
        v->protection = v->protection || guarded;
    }
}

// ───────── обмеження руху ─────────

void tof_limit_sides(float left, float right, bool front_occupied, bool rear_occupied, float *left_out, float *right_out)
{
    float thrust = (left + right) / 2.0f;
    float turn = (left - right) / 2.0f;
    bool clamp = (front_occupied && thrust > 0.0f) || (rear_occupied && thrust < 0.0f);
    if (!clamp) {
        *left_out = left;
        *right_out = right;
        return;
    }
    *left_out = turn;      // тяга обнулена, лишився лише поворот
    *right_out = -turn;
}

// ───────── тексти ─────────

static const char *k_sensor_names[TOF_SENSORS] = {"U7", "U8", "U9", "U10"};
static const char *k_place[TOF_SENSORS] = {"ліворуч-вперед", "вперед", "праворуч-вперед", "назад"};

const char *tof_sensor_name(tof_id_t id)
{
    return (unsigned)id < TOF_SENSORS ? k_sensor_names[id] : "?";
}

static void append(char *buf, size_t n, size_t *pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

#include <stdarg.h>
static void append(char *buf, size_t n, size_t *pos, const char *fmt, ...)
{
    if (*pos + 1 >= n) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *pos, n - *pos, fmt, ap);
    va_end(ap);
    if (w < 0) {
        buf[*pos] = '\0';
    } else if ((size_t)w >= n - *pos) {
        *pos = n - 1;       // обрізано
    } else {
        *pos += (size_t)w;
    }
}

size_t tof_fmt_show(char *buf, size_t n, const tof_view_t *v, bool stale)
{
    if (buf == NULL || n == 0) {
        return 0;
    }
    buf[0] = '\0';
    size_t pos = 0;
    if (v == NULL || !v->valid) {
        append(buf, n, &pos, "далекоміри: ініціалізація, захист поки вимкнено (поріг перешкоди %d мм)", TOF_OBSTACLE_MM);
        return pos;
    }
    append(buf, n, &pos, "далекоміри (поріг перешкоди %d мм, захист: %s)", TOF_OBSTACLE_MM,
           stale ? "вимкнено, знімок застарів" : (v->protection ? "діє" : "вимкнено, жоден далекомір не працює"));
    for (int i = 0; i < TOF_SENSORS; i++) {
        append(buf, n, &pos, "\n  %s (%s): ", tof_sensor_name((tof_id_t)i), k_place[i]);
        switch (v->sensor[i].state) {
        case TOF_S_OK: append(buf, n, &pos, "працює, %u мм", (unsigned)v->sensor[i].mm); break;
        case TOF_S_OUT: append(buf, n, &pos, "працює, поза діапазоном"); break;
        case TOF_S_NOT_READY: append(buf, n, &pos, "запускається, відстані ще немає"); break;
        case TOF_S_FAILED: append(buf, n, &pos, "відмова"); break;
        case TOF_S_INIT: default: append(buf, n, &pos, "ініціалізація"); break;
        }
    }
    static const char *const names[TOF_SECTORS] = {"передній", "задній"};
    for (int k = 0; k < TOF_SECTORS; k++) {
        const tof_view_sector_t *s = &v->sector[k];
        append(buf, n, &pos, "\n  сектор %s: ", names[k]);
        if (!s->guarded) {
            append(buf, n, &pos, "не охороняється");
        } else if (s->nearest < 0) {
            append(buf, n, &pos, "%s, поза діапазоном", s->occupied ? "зайнятий" : "вільний");
        } else {
            append(buf, n, &pos, "%s, %u мм (%s)", s->occupied ? "зайнятий" : "вільний", (unsigned)s->mm,
                   tof_sensor_name((tof_id_t)s->nearest));
        }
    }
    return pos;
}

size_t tof_fmt_hold_line(char *buf, size_t n, tof_sector_t sector, uint16_t mm, int nearest, bool has_first, uint32_t first_ms,
                         uint32_t now_ms)
{
    if (buf == NULL || n == 0) {
        return 0;
    }
    buf[0] = '\0';
    size_t pos = 0;
    append(buf, n, &pos, "ровер зупинено перед перешкодою: %s сектор %u мм (%s), поріг %d мм",
           sector == TOF_SECTOR_FRONT ? "передній" : "задній", (unsigned)mm,
           nearest >= 0 ? tof_sensor_name((tof_id_t)nearest) : "?", TOF_OBSTACLE_MM);
    if (has_first) {
        append(buf, n, &pos, "; перший відлік менше порога: %u мс, борти зупинено: %u мс (різниця %u мс)", (unsigned)first_ms,
               (unsigned)now_ms, (unsigned)(now_ms - first_ms));
    }
    return pos;
}
