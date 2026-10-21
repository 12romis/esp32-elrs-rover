// Чиста логіка telemetry (ADR-0001, ADR-0003). Без ESP-IDF і FreeRTOS.
#include "telemetry_core.h"

#include <stdio.h>
#include <string.h>

#include "crsf_core.h"

size_t telemetry_core_build_battery_frame(uint32_t mv, uint8_t *buf, size_t n)
{
    if (buf == NULL || n < TELEMETRY_BATTERY_FRAME_LEN) {
        return 0;
    }
    uint64_t tenths = ((uint64_t)mv + 50u) / 100u;
    if (tenths > 0x7FFFu) {
        tenths = 0x7FFFu;
    }
    memset(buf, 0, TELEMETRY_BATTERY_FRAME_LEN);
    buf[0] = CRSF_SYNC_BYTE;
    buf[1] = 10;  // тип + 8 байтів даних + CRC8
    buf[2] = TELEMETRY_TYPE_BATTERY;
    buf[3] = (uint8_t)(tenths >> 8);
    buf[4] = (uint8_t)(tenths & 0xFFu);
    // байти 5…10: струм, ємність, відсоток — нулі
    buf[11] = crsf_crc8(&buf[2], 9);
    return TELEMETRY_BATTERY_FRAME_LEN;
}

bool telemetry_core_should_send(const battery_snapshot_t *snap, uint32_t now_ms)
{
    return snap != NULL && battery_snapshot_usable(snap, now_ms);
}

void telemetry_core_link_init(tel_link_t *l)
{
    if (l != NULL) {
        memset(l, 0, sizeof(*l));
    }
}

tel_event_t telemetry_core_link_report(tel_link_t *l, bool ok)
{
    if (l == NULL) {
        return TEL_EVT_NONE;
    }
    tel_event_t ev = TEL_EVT_NONE;
    if (!l->known) {
        if (!ok) {
            ev = TEL_EVT_FAILED;     // перший успіх — не подія, перший збій — подія
        }
    } else if (l->ok && !ok) {
        ev = TEL_EVT_FAILED;
    } else if (!l->ok && ok) {
        ev = TEL_EVT_RESTORED;
    }
    l->known = true;
    l->ok = ok;
    return ev;
}

bool telemetry_core_decide(tel_link_t *snap_link, const battery_snapshot_t *snap, uint32_t now_ms, tel_event_t *ev)
{
    bool send = telemetry_core_should_send(snap, now_ms);
    tel_event_t e = telemetry_core_link_report(snap_link, send);
    if (ev != NULL) {
        *ev = e;
    }
    return send;
}

void telemetry_core_step_init(tel_step_stats_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof(*s));
    }
}

void telemetry_core_step_record(tel_step_stats_t *s, uint32_t us, uint32_t period_us)
{
    if (s == NULL) {
        return;
    }
    s->count++;
    s->last_us = us;
    if (us > s->max_us) {
        s->max_us = us;
    }
    s->sum_us += us;
    if (us > period_us) {
        s->over++;
    }
}

size_t telemetry_core_step_text(const tel_step_stats_t *s, uint32_t period_us, char *buf, size_t n)
{
    if (s == NULL || buf == NULL || n == 0) {
        return 0;
    }
    int w;
    if (s->count == 0) {
        w = snprintf(buf, n, "крок керування: вимірів ще немає");
    } else {
        w = snprintf(buf, n, "крок керування: останній %u мкс, середній %u мкс, найбільший %u мкс, період %u мкс, перевищень %u з %u",
                     (unsigned)s->last_us, (unsigned)(s->sum_us / s->count), (unsigned)s->max_us, (unsigned)period_us,
                     (unsigned)s->over, (unsigned)s->count);
    }
    if (w < 0) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)w < n ? (size_t)w : n - 1;
}
