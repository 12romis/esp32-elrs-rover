// Чиста логіка сесії (phase8-session-report, ADR-0001): див. session_core.h. Без заголовків ESP-IDF.
#include "session_core.h"

#include <math.h>
#include <string.h>

void session_init(session_t *s)
{
    memset(s, 0, sizeof(*s));
    s->prev_state = APP_STATE_DISARMED;
}

bool session_active(const session_t *s)
{
    return s != NULL && s->active;
}

static void begin(session_t *s, const session_in_t *in)
{
    memset(s, 0, sizeof(*s));
    s->active = true;
    s->seq = in->next_seq;
    s->start_ms = in->now_ms;
    s->last_ms = in->now_ms;
    s->have_last_ms = true;
    s->sum.seq = in->next_seq;
    s->sum.hold_min_sensor = -1;
    s->prev_state = in->state;
    s->prev_ctl_on = in->tof_control_on;
    s->sum.left_enc_seen = in->left_avail;
    s->sum.right_enc_seen = in->right_avail;
    if (in->battery_valid) {
        s->sum.voltage_start_valid = true;
        s->sum.voltage_start_mv = in->battery_mv;
    }
}

// Напруга й події утримання кроку, що належить сесії.
static void sample_voltage(session_t *s, const session_in_t *in)
{
    s->last_mv_valid = in->battery_valid;
    if (in->battery_valid) {
        s->last_mv = in->battery_mv;
        if (!s->sum.voltage_min_valid || in->battery_mv < s->sum.voltage_min_mv) {
            s->sum.voltage_min_valid = true;
            s->sum.voltage_min_mv = in->battery_mv;
        }
    }
}

static void note_hold(session_t *s, const session_in_t *in)
{
    session_summary_t *m = &s->sum;
    m->hold_count++;
    if (!m->hold_min_valid || in->hold_mm < m->hold_min_mm) {
        m->hold_min_valid = true;
        m->hold_min_mm = in->hold_mm;
        m->hold_min_sensor = in->hold_nearest;
    }
    for (int i = 0; i < SESSION_SENSORS; i++) {
        if (in->hold_below_mask & (1u << i)) {
            m->hold_by_sensor[i]++;
        }
    }
}

// Час стану й режиму: інтервал від попереднього кроку належить стану й режиму попереднього кроку (вони трималися весь інтервал).
// Рух: приріст енкодерів і потужність цього кроку належать інтервалу, що закінчується на ньому.
static void accumulate(session_t *s, const session_in_t *in)
{
    uint32_t dt = (uint32_t)(in->now_ms - s->last_ms);
    s->last_ms = in->now_ms;
    session_summary_t *m = &s->sum;
    if (s->prev_state == APP_STATE_OBSTACLE_HOLD) {
        m->hold_total_ms += dt;
    }
    if (!s->prev_ctl_on) {
        m->tof_control_off_ms += dt;
    }
    s->prev_state = in->state;
    s->prev_ctl_on = in->tof_control_on;
    if (in->left_avail || in->right_avail) {
        s->any_avail = true;
    }
    s->sum.left_enc_seen = s->sum.left_enc_seen || in->left_avail;
    s->sum.right_enc_seen = s->sum.right_enc_seen || in->right_avail;
    bool moving = in->left_permille != 0 || in->right_permille != 0;
    if (!moving || dt == 0) {
        return;
    }
    m->moving_ms += dt;
    double d = 0.0;
    bool have_d = true;
    if (in->left_avail && in->right_avail) {
        d = ((double)in->left_dist_cm + (double)in->right_dist_cm) / 2.0;
    } else if (in->left_avail) {
        d = in->left_dist_cm;
    } else if (in->right_avail) {
        d = in->right_dist_cm;
    } else {
        have_d = false;
    }
    if (have_d) {
        s->dist_cm += fabs(d);
    }
    double l = fabs((double)in->left_rpm);
    double r = fabs((double)in->right_rpm);
    if (in->left_avail) {
        s->left_rpm_ms += l * dt;
        s->left_ms += dt;
        if (l > m->left_peak_rpm) {
            m->left_peak_rpm = (float)l;
        }
    }
    if (in->right_avail) {
        s->right_rpm_ms += r * dt;
        s->right_ms += dt;
        if (r > m->right_peak_rpm) {
            m->right_peak_rpm = (float)r;
        }
    }
    // Пряма їзда: задані значення бортів однакові й ненульові, обидва енкодери доступні.
    if (in->left_avail && in->right_avail && in->cmd_left == in->cmd_right && in->cmd_left != 0.0f) {
        s->str_left_rpm_ms += l * dt;
        s->str_right_rpm_ms += r * dt;
        s->str_ms += dt;
    }
}

static void finish(session_t *s, const session_in_t *in, session_summary_t *out)
{
    session_summary_t m = s->sum;
    m.duration_ms = (uint32_t)(in->now_ms - s->start_ms);
    if (in->state == APP_STATE_FAILSAFE) {
        m.end_reason = in->failsafe_cause == APP_FS_VOLTAGE ? SESSION_END_VOLTAGE : SESSION_END_LINK_LOST;
    } else {
        m.end_reason = SESSION_END_DISARM;
    }
    m.voltage_end_valid = s->last_mv_valid;
    m.voltage_end_mv = s->last_mv;
    m.distance_valid = s->any_avail;
    m.distance_cm = (float)s->dist_cm;
    m.left_speed_valid = s->left_ms > 0;
    m.right_speed_valid = s->right_ms > 0;
    if (m.left_speed_valid) {
        m.left_avg_rpm = (float)(s->left_rpm_ms / s->left_ms);
    } else {
        m.left_peak_rpm = 0.0f;
    }
    if (m.right_speed_valid) {
        m.right_avg_rpm = (float)(s->right_rpm_ms / s->right_ms);
    } else {
        m.right_peak_rpm = 0.0f;
    }
    if (s->str_ms > 0) {
        double al = s->str_left_rpm_ms / s->str_ms;
        double ar = s->str_right_rpm_ms / s->str_ms;
        double half = (al + ar) / 2.0;
        if (half > 0.0) {
            m.imbalance_valid = true;
            m.imbalance_pct = (float)((al - ar) / half * 100.0);
        }
    }
    *out = m;
    s->active = false;
}

bool session_step(session_t *s, const session_in_t *in, session_summary_t *out)
{
    if (s == NULL || in == NULL) {
        return false;
    }
    bool driving = app_state_is_driving(in->state);
    if (!s->active) {
        if (!driving) {
            return false;
        }
        begin(s, in);
        sample_voltage(s, in);
        if (in->hold_enter) {
            note_hold(s, in);
        }
        return false;
    }
    accumulate(s, in);
    if (!driving) {
        if (out != NULL) {
            finish(s, in, out);
        } else {
            s->active = false;
        }
        return out != NULL;
    }
    sample_voltage(s, in);
    if (in->hold_enter) {
        note_hold(s, in);
    }
    return false;
}
