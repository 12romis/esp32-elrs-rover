// Чиста логіка руху. Без заголовків ESP-IDF і FreeRTOS.
#include "drive_core.h"

#include <stdio.h>

#include "battery_calib.h"

static float absf(float x)
{
    return x < 0.0f ? -x : x;
}

void drive_mix(float throttle, float steer, float *left, float *right)
{
    float l = throttle + steer;
    float r = throttle - steer;
    float m = 1.0f;
    if (absf(l) > m) {
        m = absf(l);
    }
    if (absf(r) > m) {
        m = absf(r);
    }
    *left = l / m;
    *right = r / m;
}

static drive_side_t side_from(float v)
{
    if (!(v == v)) {  // NaN: захист від невизначеної поведінки, рівень вимкнений
        v = 0.0f;
    }
    drive_side_t s = {0, 0, 0};
    unsigned pm = (unsigned)(absf(v) * 1000.0f + 0.5f);
    if (pm > DRIVE_PWM_MAX_PERMILLE) {
        pm = DRIVE_PWM_MAX_PERMILLE;
    }
    s.duty_permille = (uint16_t)pm;
    if (pm != 0) {
        s.dir1 = v > 0.0f ? 1 : 0;
        s.dir2 = v > 0.0f ? 0 : 1;
    }
    return s;
}

drive_outputs_t drive_outputs_from(float left, float right)
{
    drive_outputs_t o;
    o.left = side_from(left);
    o.right = side_from(right);
    o.stby = 1;
    return o;
}

drive_outputs_t drive_outputs_off(void)
{
    drive_outputs_t o = {{0, 0, 0}, {0, 0, 0}, 0};
    return o;
}

// ---------- Крок керування ----------

void drive_ctl_init(drive_ctl_t *c)
{
    c->window_active = false;
    c->accepted_ms = 0;
    c->outputs = drive_outputs_off();
    c->left_permille = 0;
    c->right_permille = 0;
    c->min_mv = 0;
}

static int16_t signed_pm(drive_side_t s)
{
    if (s.dir1 && !s.dir2) {
        return (int16_t)s.duty_permille;
    }
    if (s.dir2 && !s.dir1) {
        return (int16_t)-(int16_t)s.duty_permille;
    }
    return 0;
}

static void clear_window(drive_ctl_t *c)
{
    c->window_active = false;
    c->outputs = drive_outputs_off();
    c->left_permille = 0;
    c->right_permille = 0;
}

static bool in_range(float x)
{
    return x == x && x >= -1.0f && x <= 1.0f;
}

void drive_ctl_step(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *o)
{
    *o = (drive_ctl_out_t){0};
    o->out = drive_outputs_off();
    o->reason = DRIVE_REASON_NONE;

    if (in->state != APP_STATE_ARMED) {
        clear_window(c);  // поза ARMED виходи завжди вимкнені (AC-08)
    }

    bool usable = battery_snapshot_usable(&in->battery, in->now_ms);

    // Відсічка по напрузі в ARMED має перевагу над запитами (AC-14): знімок застарів, невалідний або напруга нижча за поріг.
    bool cutoff = false;
    if (in->state == APP_STATE_ARMED) {
        if (!usable) {
            cutoff = true;
            o->stop = DRIVE_STOP_STALE_VOLTAGE;
        } else if (battery_is_below_cutoff(in->battery.mv)) {
            cutoff = true;
            o->stop = DRIVE_STOP_LOW_VOLTAGE;
        }
    }
    if (cutoff) {
        o->mv = in->battery.mv;
        o->min_mv = !c->window_active ? 0 : (in->battery.mv < c->min_mv ? in->battery.mv : c->min_mv);
        clear_window(c);
        o->has_state_request = true;
        o->state_request = APP_STATE_FAILSAFE;
    }

    switch (in->req.kind) {
    case DRIVE_REQ_ARM:
        if (in->state != APP_STATE_DISARMED) {
            o->reason = DRIVE_REASON_NOT_DISARMED;
        } else if (!usable) {
            o->reason = DRIVE_REASON_NO_MEASUREMENT;
        } else if (battery_is_below_cutoff(in->battery.mv)) {
            o->reason = DRIVE_REASON_LOW_VOLTAGE;
            o->mv = in->battery.mv;
        } else {
            o->accepted = true;
            o->has_state_request = true;
            o->state_request = APP_STATE_ARMED;
        }
        break;
    case DRIVE_REQ_DISARM:
        o->accepted = true;  // disarm приймається в кожному стані (AC-09)
        if (c->window_active) {
            o->min_mv = (usable && in->battery.mv < c->min_mv) ? in->battery.mv : c->min_mv;
        }
        clear_window(c);
        if (!cutoff) {
            o->stop = DRIVE_STOP_DISARM;
            if (in->state == APP_STATE_ARMED) {
                o->has_state_request = true;
                o->state_request = APP_STATE_DISARMED;
            }
        }
        break;
    case DRIVE_REQ_DRIVE:
        if (in->state != APP_STATE_ARMED) {
            o->reason = DRIVE_REASON_NOT_ARMED;
        } else if (cutoff) {
            o->reason = usable ? DRIVE_REASON_LOW_VOLTAGE : DRIVE_REASON_NO_MEASUREMENT;
        } else if (!in_range(in->req.throttle) || !in_range(in->req.steer)) {
            o->reason = DRIVE_REASON_OUT_OF_RANGE;
        } else {
            float l, r;
            drive_mix(in->req.throttle, in->req.steer, &l, &r);
            c->outputs = drive_outputs_from(l, r);
            c->left_permille = signed_pm(c->outputs.left);
            c->right_permille = signed_pm(c->outputs.right);
            c->window_active = true;
            c->accepted_ms = in->now_ms;
            c->min_mv = in->battery.mv;
            o->accepted = true;
        }
        break;
    case DRIVE_REQ_NONE:
        break;
    }

    // Спливання вікна: виходи вимикаються, і лише потім запитується DISARMED.
    if (c->window_active && (uint32_t)(in->now_ms - c->accepted_ms) >= DRIVE_WINDOW_MS) {
        o->min_mv = c->min_mv;
        o->stop = DRIVE_STOP_WINDOW_EXPIRED;
        clear_window(c);
        o->has_state_request = true;
        o->state_request = APP_STATE_DISARMED;
    }

    if (c->window_active) {
        if (in->battery.mv < c->min_mv) {
            c->min_mv = in->battery.mv;
        }
        o->out = c->outputs;
        o->left_permille = c->left_permille;
        o->right_permille = c->right_permille;
        o->remaining_ms = DRIVE_WINDOW_MS - (uint32_t)(in->now_ms - c->accepted_ms);
        o->min_mv = c->min_mv;
    }
}

void drive_reason_text(drive_reason_t r, uint32_t mv, char *buf, size_t n)
{
    switch (r) {
    case DRIVE_REASON_NOT_DISARMED:
        snprintf(buf, n, "команда arm дозволена лише в стані DISARMED");
        break;
    case DRIVE_REASON_NOT_ARMED:
        snprintf(buf, n, "команда drive дозволена лише в стані ARMED");
        break;
    case DRIVE_REASON_LOW_VOLTAGE:
        snprintf(buf, n, "напруга %u мВ нижча за поріг %u мВ", (unsigned)mv, (unsigned)BATTERY_CUTOFF_MV);
        break;
    case DRIVE_REASON_NO_MEASUREMENT:
        snprintf(buf, n, "вимірювання напруги ще немає або застаріло");
        break;
    case DRIVE_REASON_OUT_OF_RANGE:
        snprintf(buf, n, "значення поза діапазоном від −1 до +1");
        break;
    case DRIVE_REASON_TRANSITION_REFUSED:
        snprintf(buf, n, "перехід стану відхилено автоматом станів");
        break;
    case DRIVE_REASON_NONE:
        snprintf(buf, n, "відмови немає");
        break;
    }
}

const char *drive_stop_text(drive_stop_t s)
{
    switch (s) {
    case DRIVE_STOP_WINDOW_EXPIRED: return "команда руху спливла, ровер роззброєно";
    case DRIVE_STOP_LOW_VOLTAGE:    return "напруга нижча за поріг, FAILSAFE";
    case DRIVE_STOP_STALE_VOLTAGE:  return "вимірювання напруги застаріло або відсутнє, FAILSAFE";
    case DRIVE_STOP_DISARM:         return "роззброєно командою disarm";
    case DRIVE_STOP_NONE:           break;
    }
    return "немає";
}

void drive_disarm_text(app_state_t state_before, bool has_req, app_state_t req, drive_stop_t stop, char *buf, size_t n)
{
    if (stop == DRIVE_STOP_LOW_VOLTAGE || stop == DRIVE_STOP_STALE_VOLTAGE) {
        snprintf(buf, n, "спрацювала відсічка по напрузі, ровер у FAILSAFE");
    } else if (has_req && req == APP_STATE_DISARMED) {
        snprintf(buf, n, "ровер роззброєно");
    } else {
        snprintf(buf, n, "стан %s не змінено: DISARMED досяжний лише з ARMED", app_state_name(state_before));
    }
}
