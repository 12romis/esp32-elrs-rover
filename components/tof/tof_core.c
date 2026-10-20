// Чиста логіка далекомірів (phase6-tof, ADR-0001, ADR-0003): без заголовків ESP-IDF, перевіряється на хості.
#include "tof_core.h"

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
        v->protection = v->protection || guarded;
    }
}
