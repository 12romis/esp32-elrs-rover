// Чиста логіка енкодерів коліс (ADR-0002): приріст за модулем межі, знак колеса, накопичувач, обнулення через зсув,
// швидкість як приріст на фактичний час між вибірками.
#include "encoders_core.h"

#include <string.h>

#include "encoders_calib.h"

static const int s_sign[ENC_WHEELS] = ENC_DIR_SIGN;

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
    bool interval = s->primed && dt_us > 0;
    for (int i = 0; i < ENC_WHEELS; i++) {
        enc_wheel_state_t *w = &s->w[i];
        if (!w->available) continue;
        if (s->primed) {
            int64_t d = (int64_t)s_sign[i] * enc_core_delta(w->last_raw, raw[i]);
            w->accum += d;
            if (interval) w->speed_pps = (float)((double)d * 1e6 / (double)dt_us);
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
