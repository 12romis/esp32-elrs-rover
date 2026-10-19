// Чиста логіка енкодерів коліс (ADR-0002): приріст за модулем межі, знак колеса, накопичувач, обнулення через зсув,
// швидкість як приріст на фактичний час між вибірками.
#include "encoders_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "encoders_calib.h"

static const int s_sign[ENC_WHEELS] = ENC_DIR_SIGN;
static const char *const s_name[ENC_WHEELS] = { "FL", "FR", "RL", "RR" };

#define ENC_PI 3.14159265358979323846
#define ENC_CIRCUMFERENCE_CM (ENC_PI * WHEEL_DIAMETER_MM / 10.0)   // довжина кола колеса, см

void enc_core_init(enc_state_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < ENC_WHEELS; i++) s->w[i].available = true;
}

int32_t enc_core_delta(int32_t prev_raw, int32_t raw)
{
    // Апаратний лічильник на ±ENC_HW_LIMIT скидається в нуль, тож сирі значення узгоджені за модулем межі:
    // −1 і ENC_HW_LIMIT − 1 — те саме положення. Беремо найкоротший шлях між ними.
    int32_t d = (raw - prev_raw) % ENC_HW_LIMIT;
    if (d > ENC_STEP_MAX) d -= ENC_HW_LIMIT;
    if (d <= -ENC_STEP_MAX) d += ENC_HW_LIMIT;
    return d;
}

void enc_core_sample(enc_state_t *s, const int32_t raw[ENC_WHEELS], uint64_t now_us)
{
    uint64_t dt_us = now_us - s->last_us;   // беззнакова різниця: переповнення мітки не заважає
    bool interval = s->primed && dt_us >= ENC_MIN_INTERVAL_US;
    for (int i = 0; i < ENC_WHEELS; i++) {
        enc_wheel_state_t *w = &s->w[i];
        if (!w->available) continue;
        if (s->primed) {
            int32_t d = s_sign[i] * enc_core_delta(w->last_raw, raw[i]);
            w->accum += d;
            w->pending += d;   // коротких вибірок до інтервалу одиниці, тож межа int32_t недосяжна
            if (interval) {
                w->speed_pps = (float)((double)w->pending * 1e6 / (double)dt_us);
                w->pending = 0;
            }
        }
        w->last_raw = raw[i];
    }
    if (interval) s->intervals++;
    if (!s->primed || interval) s->last_us = now_us;
    s->primed = true;
}

void enc_core_reset(enc_state_t *s)
{
    for (int i = 0; i < ENC_WHEELS; i++) {
        if (s->w[i].available) s->w[i].offset = s->w[i].accum;
    }
}

int64_t enc_core_count(const enc_state_t *s, enc_wheel_t wheel)
{
    return s->w[wheel].accum - s->w[wheel].offset;
}

void enc_core_set_unavailable(enc_state_t *s, enc_wheel_t wheel, const char *reason)
{
    enc_wheel_state_t *w = &s->w[wheel];
    w->available = false;
    snprintf(w->reason, sizeof(w->reason), "%s", reason ? reason : "невідома причина");
    w->speed_pps = 0.0f;
}

unsigned enc_core_available_count(const enc_state_t *s)
{
    unsigned n = 0;
    for (int i = 0; i < ENC_WHEELS; i++) n += s->w[i].available ? 1u : 0u;
    return n;
}

double enc_core_revs(int64_t count)
{
    return (double)count / ENC_PULSES_PER_REV;
}

double enc_core_cm(int64_t count)
{
    return enc_core_revs(count) * ENC_CIRCUMFERENCE_CM;
}

double enc_core_rpm(float speed_pps)
{
    return (double)speed_pps * 60.0 / ENC_PULSES_PER_REV;
}

double enc_core_cm_s(float speed_pps)
{
    return (double)speed_pps / ENC_PULSES_PER_REV * ENC_CIRCUMFERENCE_CM;
}

// Дописує форматований текст у буфер, не виходячи за його межі; *len — поточна довжина тексту.
static void append(char *buf, size_t n, size_t *len, const char *fmt, ...)
{
    if (n == 0 || *len >= n - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *len, n - *len, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    *len += (size_t)w;
    if (*len > n - 1) *len = n - 1;   // vsnprintf обрізав: довжина — те, що реально в буфері
}

// Перелік назв коліс із заданою доступністю через кому.
static void append_names(const enc_state_t *s, bool available, char *buf, size_t n, size_t *len)
{
    bool first = true;
    for (int i = 0; i < ENC_WHEELS; i++) {
        if (s->w[i].available != available) continue;
        append(buf, n, len, first ? "%s" : ", %s", s_name[i]);
        first = false;
    }
}

size_t enc_core_format_snapshot(const enc_state_t *s, char *buf, size_t n)
{
    size_t len = 0;
    if (n > 0) buf[0] = '\0';
    if (s->primed) {
        append(buf, n, &len, "енкодери: вибірка %llu мс від старту, інтервалів %lu\n",
               (unsigned long long)(s->last_us / 1000u), (unsigned long)s->intervals);
    } else {
        append(buf, n, &len, "енкодери: вибірок ще не було\n");
    }
    for (int i = 0; i < ENC_WHEELS; i++) {
        const enc_wheel_state_t *w = &s->w[i];
        if (!w->available) {
            append(buf, n, &len, "%s: недоступне — %s\n", s_name[i], w->reason);
            continue;
        }
        int64_t c = enc_core_count(s, (enc_wheel_t)i);
        append(buf, n, &len, "%s: %lld імп, %.2f об, %.1f об/хв, %.1f см, %.1f см/с\n", s_name[i], (long long)c,
               enc_core_revs(c), enc_core_rpm(w->speed_pps), enc_core_cm(c), enc_core_cm_s(w->speed_pps));
    }
    return len;
}

size_t enc_core_format_reset(const enc_state_t *s, char *buf, size_t n)
{
    size_t len = 0;
    if (n == 0) return 0;
    buf[0] = '\0';
    unsigned avail = enc_core_available_count(s);
    if (avail == 0) {
        append(buf, n, &len, "обнуляти нічого: енкодери недоступні (");
        append_names(s, false, buf, n, &len);
        append(buf, n, &len, ")");
        return len;
    }
    append(buf, n, &len, "лічильники й відстань обнулено: ");
    append_names(s, true, buf, n, &len);
    if (avail < ENC_WHEELS) {
        append(buf, n, &len, "; недоступні: ");
        append_names(s, false, buf, n, &len);
    }
    return len;
}

size_t enc_core_format_status(const enc_state_t *s, char *buf, size_t n)
{
    size_t len = 0;
    if (n == 0) return 0;
    buf[0] = '\0';
    unsigned avail = enc_core_available_count(s);
    append(buf, n, &len, "енкодери: %u з %d доступні", avail, ENC_WHEELS);
    if (avail < ENC_WHEELS) {
        append(buf, n, &len, ", недоступні: ");
        append_names(s, false, buf, n, &len);
    }
    return len;
}
