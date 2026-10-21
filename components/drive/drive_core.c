// Чиста логіка руху. Без заголовків ESP-IDF і FreeRTOS.
#include "drive_core.h"

#include <stdio.h>
#include <string.h>

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
    c->remote_active = false;
    c->link_alive = false;
    c->link_since_ms = 0;
    c->sw_base = CRSF_SW_UNKNOWN;
    c->win_mix_l = 0.0f;
    c->win_mix_r = 0.0f;
    c->armed_prev = false;
    c->pid_mode = PID_MODE_UNCONFIGURED;
    pid_side_reset(&c->pid_side[0]);
    pid_side_reset(&c->pid_side[1]);
    c->ramp_rpm[0] = 0.0f;
    c->ramp_rpm[1] = 0.0f;
    c->last_ms = 0;
    c->enc_seen = false;
    c->enc_intervals = 0;
    c->enc_t_us = 0;
    c->enc_stale = false;
    pid_trace_acc_reset(&c->trace);
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

// Умови озброєння з пульта без фронту: стан → звʼязок → напруга → нейтраль (spec AC-04).
static drive_block_t arm_conditions(const drive_ctl_t *c, app_state_t st, const battery_snapshot_t *b,
                                    const crsf_intent_t *it, uint32_t now_ms)
{
    if (st != APP_STATE_DISARMED) {
        return DRIVE_BLOCK_NOT_DISARMED;
    }
    if (!c->remote_active || !c->link_alive) {
        return DRIVE_BLOCK_NO_LINK;
    }
    if (!battery_snapshot_usable(b, now_ms)) {
        return DRIVE_BLOCK_NO_MEASUREMENT;
    }
    if (battery_is_below_cutoff(b->mv)) {
        return DRIVE_BLOCK_LOW_VOLTAGE;
    }
    if (!it->throttle_neutral || !it->steer_neutral) {
        return DRIVE_BLOCK_NOT_NEUTRAL;
    }
    return DRIVE_BLOCK_NONE;
}

drive_block_t drive_arm_block(const drive_ctl_t *c, app_state_t st, const battery_snapshot_t *b,
                              const crsf_intent_t *intent, uint32_t now_ms)
{
    drive_block_t blk = arm_conditions(c, st, b, intent, now_ms);
    if (blk != DRIVE_BLOCK_NONE) {
        return blk;
    }
    // Фронт: тумблер має щойно перемкнутися з DISARM. База ARM чи «невідомо» означає, що ні.
    return c->sw_base == CRSF_SW_DISARM ? DRIVE_BLOCK_NONE : DRIVE_BLOCK_NO_EDGE;
}

const char *drive_block_text(drive_block_t b)
{
    switch (b) {
    case DRIVE_BLOCK_NONE:            return "можна: перемкніть тумблер з DISARM у ARM";
    case DRIVE_BLOCK_NOT_DISARMED:    return "ровер не в стані DISARMED";
    case DRIVE_BLOCK_NO_LINK:         return "звʼязку з пультом немає";
    case DRIVE_BLOCK_NO_MEASUREMENT:  return "вимірювання напруги немає або застаріло";
    case DRIVE_BLOCK_LOW_VOLTAGE:     return "напруга нижча за поріг";
    case DRIVE_BLOCK_NOT_NEUTRAL:     return "стік не в нейтралі";
    case DRIVE_BLOCK_NO_EDGE:         return "тумблер не перемикався з DISARM: поверніть його в DISARM і знову в ARM";
    case DRIVE_BLOCK_PID_SAVING:      return "триває збереження набору регулятора — спробуйте ще раз за мить";
    case DRIVE_BLOCK_PID_MODE:        return "триває зміна режиму регулятора — спробуйте ще раз за мить";
    }
    return "?";
}

void drive_lat_reset(drive_lat_t *l)
{
    *l = (drive_lat_t){0};
}

void drive_lat_add(drive_lat_t *l, uint32_t frame_ms, uint32_t applied_ms)
{
    uint32_t d = (uint32_t)(applied_ms - frame_ms);
    if (l->n == 0 || d < l->min_ms) {
        l->min_ms = d;
    }
    if (d > l->max_ms) {
        l->max_ms = d;
    }
    l->sum_ms += d;
    l->n++;
}

void drive_lat_fmt(const drive_lat_t *l, char *buf, size_t n)
{
    unsigned avg = l->n ? (unsigned)((l->sum_ms + l->n / 2) / l->n) : 0;
    snprintf(buf, n, "затримка кадр → борти: %u кадрів, мін %u, сер %u, макс %u мс", (unsigned)l->n,
             (unsigned)l->min_ms, avg, (unsigned)l->max_ms);
}

crsf_switch_t drive_switch_shown(const drive_ctl_t *c)
{
    return c->remote_active && c->link_alive ? c->sw_base : CRSF_SW_UNKNOWN;
}

drive_fsx_t drive_failsafe_exit_state(const drive_ctl_t *c, app_failsafe_cause_t cause, const crsf_intent_t *intent,
                                      uint32_t now_ms)
{
    if (cause != APP_FS_LINK_LOST) {
        return DRIVE_FSX_RESET_ONLY;   // відсічка по напрузі: лише апаратне скидання (spec AC-09)
    }
    if (!c->remote_active || !c->link_alive) {
        return DRIVE_FSX_NO_LINK;
    }
    if ((uint32_t)(now_ms - c->link_since_ms) < DRIVE_LINK_STABLE_MS) {   // беззнаково: без зламу через 2^31 мс
        return DRIVE_FSX_STABILIZING;
    }
    if (intent->sw != CRSF_SW_DISARM) {
        return DRIVE_FSX_SWITCH_ARM;
    }
    return DRIVE_FSX_READY;
}

const char *drive_fsx_text(drive_fsx_t x)
{
    switch (x) {
    case DRIVE_FSX_READY:       return "умови виходу з аварійної зупинки виконано";
    case DRIVE_FSX_RESET_ONLY:  return "після відсічки по напрузі вихід лише скиданням плати чи перемиканням живлення";
    case DRIVE_FSX_NO_LINK:     return "звʼязку з пультом немає: вихід можливий після відновлення";
    case DRIVE_FSX_STABILIZING: return "звʼязок ще не стабільний: потрібна секунда без втрат";
    case DRIVE_FSX_SWITCH_ARM:  return "тумблер у ARM: опустіть його в DISARM";
    }
    return "?";
}

// Звʼязок з пультом (ADR-0001, ADR-0003): стендовий режим закінчується першим валідним кадром каналів; далі звʼязок
// живий, доки від мітки останнього кадру минуло не більше DRIVE_LINK_TIMEOUT_MS. Втрата й відновлення скидають
// базу тумблера (перший кадр після них лише встановлює її, фронтом не є) і дають подію для журналу.
static void update_link(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *o)
{
    const crsf_snapshot_t *rc = &in->rc;
    bool appeared = false;
    if (rc->have_frame && !c->remote_active) {
        c->remote_active = true;
        c->link_alive = false;
        c->sw_base = CRSF_SW_UNKNOWN;
        o->events |= DRIVE_EVT_REMOTE_APPEARED;
        appeared = true;
    }
    if (!c->remote_active) {
        return;
    }
    // Вік кадру беззнаковий (control бере now після знімка, тож now ≥ t_ms): переповнення мс не впливає (sad.md §4).
    bool alive = rc->have_frame && (uint32_t)(in->now_ms - rc->t_ms) <= DRIVE_LINK_TIMEOUT_MS;
    if (alive && !c->link_alive) {
        c->link_alive = true;
        c->sw_base = CRSF_SW_UNKNOWN;
        c->link_since_ms = rc->t_ms;
        if (!appeared) {
            o->events |= DRIVE_EVT_LINK_RESTORED;
        }
    } else if (!alive && c->link_alive) {
        c->link_alive = false;
        c->sw_base = CRSF_SW_UNKNOWN;
        o->events |= DRIVE_EVT_LINK_LOST;
    }
}

// ---------- Регулятор швидкості бортів (phase5-pid; ADR-0001, spec «Поведінка регулятора») ----------

// Колеса борту в порядку FL, FR, RL, RR: лівий — FL і RL, правий — FR і RR.
static const enc_wheel_t SIDE_WHEEL[2][2] = { {ENC_FL, ENC_RL}, {ENC_FR, ENC_RR} };
static const char *const WHEEL_NAME[ENC_WHEELS] = { "FL", "FR", "RL", "RR" };

static void pid_reset_sides(drive_ctl_t *c)
{
    for (int b = 0; b < 2; b++) {
        pid_side_reset(&c->pid_side[b]);
        c->ramp_rpm[b] = 0.0f;
    }
}

// Знімок енкодерів застарів (spec AC-12b): є, але від мітки останнього інтервалу минуло понад DRIVE_ENC_STALE_MS
// (або інтервалу ще не було). Вік беззнаковий у мс: now_ms і мітка — одна шкала esp_timer.
static bool enc_stale(const drive_ctl_in_t *in)
{
    if (!in->enc.valid) return false;
    if (in->enc.intervals == 0) return true;
    uint32_t t_ms = (uint32_t)(in->enc.t_us / 1000u);
    return (uint32_t)(in->now_ms - t_ms) > DRIVE_ENC_STALE_MS;
}

// Режим і швидкість борту для такту: без регулятора, без енкодерів, за одним чи двома колесами. У ARMED діє режим,
// зафіксований при озброєнні (spec AC-15); поза ARMED показ бере режим поточного набору (spec AC-03, AC-06, AC-12).
static void side_view(const drive_ctl_t *c, const drive_ctl_in_t *in, int b, bool stale, pid_side_view_t *v)
{
    memset(v, 0, sizeof *v);
    pid_mode_t mode = app_state_is_driving(in->state) ? c->pid_mode : in->pid.mode;
    enc_wheel_t a = SIDE_WHEEL[b][0], d = SIDE_WHEEL[b][1];
    bool ok = in->enc.valid && !stale;
    pid_src_t src = pid_side_speed(ok && in->enc.avail[a], in->enc.rpm[a], ok && in->enc.avail[d], in->enc.rpm[d],
                                   &v->speed_rpm);
    if (src == PID_SRC_NONE) {
        v->mode = PID_SIDE_NO_ENC;
    } else if (mode != PID_MODE_ON) {
        v->mode = PID_SIDE_OPEN;
    } else if (src == PID_SRC_ONE) {
        v->mode = PID_SIDE_ONE;
        v->missing = WHEEL_NAME[in->enc.avail[a] ? d : a];
    } else {
        v->mode = PID_SIDE_TWO;
    }
}

size_t drive_pid_boards_text(pid_mode_t mode, const bool avail[ENC_WHEELS], char *buf, size_t n)
{
    static const char *const SIDE_NAME[2] = { "лівий борт", "правий борт" };
    size_t len = 0;
    if (n) buf[0] = 0;
    for (int b = 0; b < 2 && len < n; b++) {
        enc_wheel_t a = SIDE_WHEEL[b][0], d = SIDE_WHEEL[b][1];
        const char *sep = b ? "; " : "";
        int w;
        if (!avail[a] && !avail[d]) {
            w = snprintf(buf + len, n - len, "%s%s: без регулятора — немає доступного енкодера (%s, %s)", sep,
                         SIDE_NAME[b], WHEEL_NAME[a], WHEEL_NAME[d]);
        } else if (mode != PID_MODE_ON) {
            w = snprintf(buf + len, n - len, "%s%s: без регулятора (регулятор %s)", sep, SIDE_NAME[b],
                         mode == PID_MODE_OFF ? "вимкнений" : "не налаштований");
        } else if (!avail[a] || !avail[d]) {
            w = snprintf(buf + len, n - len, "%s%s: за одним колесом (%s недоступне)", sep, SIDE_NAME[b],
                         WHEEL_NAME[avail[a] ? d : a]);
        } else {
            w = snprintf(buf + len, n - len, "%s%s: за двома колесами", sep, SIDE_NAME[b]);
        }
        if (w < 0) break;
        len += (size_t)w < n - len ? (size_t)w : n - len - 1;
    }
    return len;
}

// Виходи бортів для виходу мікшера (l, r): з регулятором — частка потужності з кроку pid_core на новому знімку
// (між знімками тримається); без регулятора — вихід мікшера, як у фазі 4 (spec AC-16).
static void regulate(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *o, float l, float r, float dt_s,
                     bool fresh, float dt_enc_s, bool stale)
{
    float lim[2];
    tof_limit_sides(l, r, c->occ_front, c->occ_rear, &lim[0], &lim[1]);   // зайнятий сектор: тяга в його бік нуль, поворот лишається
    const float mix[2] = {lim[0], lim[1]};
    const float limit = (float)DRIVE_PWM_MAX_PERMILLE / 1000.0f;
    float out[2];
    for (int b = 0; b < 2; b++) {
        pid_side_view_t *v = &o->pid_view[b];
        side_view(c, in, b, stale, v);
        v->mix = mix[b];
        if (v->mode != PID_SIDE_TWO && v->mode != PID_SIDE_ONE) {
            pid_side_reset(&c->pid_side[b]);
            c->ramp_rpm[b] = 0.0f;
            out[b] = mix[b];
            continue;
        }
        float req = pid_target_from_mix(mix[b]);
        c->ramp_rpm[b] = pid_ramp(c->ramp_rpm[b], req, in->pid.v[PID_PARAM_RAMP], dt_s);
        float tgt = c->ramp_rpm[b];
        if (tgt == 0.0f) {
            pid_side_reset(&c->pid_side[b]);
            out[b] = 0.0f;
        } else if (fresh) {
            out[b] = pid_side_step(&c->pid_side[b], &in->pid, tgt, v->speed_rpm, dt_enc_s, limit);
        } else {
            out[b] = c->pid_side[b].out;
            if ((out[b] > 0.0f) != (tgt > 0.0f)) out[b] = 0.0f;   // ціль змінила знак до нового знімка
        }
        v->target_rpm = tgt;
        v->out = out[b];
    }
    o->out = drive_outputs_from(out[0], out[1]);
    o->left_permille = signed_pm(o->out.left);
    o->right_permille = signed_pm(o->out.right);
}

void drive_ctl_step(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *o)
{
    *o = (drive_ctl_out_t){0};
    o->out = drive_outputs_off();
    o->reason = DRIVE_REASON_NONE;

    // Регулятор (phase5-pid): dt розгону — інтервал між тактами керування; новий знімок енкодерів — змінився лічильник
    // інтервалів, dt кроку — різниця їхніх міток; режим фіксується при вході в ARMED (spec AC-15, AC-16).
    uint32_t dt_ms = c->last_ms != 0 ? (uint32_t)(in->now_ms - c->last_ms) : 20u;
    c->last_ms = in->now_ms;
    float dt_s = (float)dt_ms / 1000.0f;
    // Далекоміри (phase6-tof, ADR-0001): знімок свіжий, якщо не старший за TOF_STALE_MS; застарілий чи відсутній — сектори
    // не охороняються й не зайняті, подія один раз (spec AC-06, AC-07).
    bool driving = app_state_is_driving(in->state);
    bool tof_ok = in->tof.valid && (uint32_t)(in->now_ms - in->tof.t_ms) <= TOF_STALE_MS;
    c->occ_front = tof_ok && in->tof.sector[TOF_SECTOR_FRONT].occupied;
    c->occ_rear = tof_ok && in->tof.sector[TOF_SECTOR_REAR].occupied;
    if (driving) {
        if (!tof_ok && !c->tof_stale) {
            c->tof_stale = true;
            o->events |= DRIVE_EVT_TOF_STALE;
        } else if (tof_ok && c->tof_stale) {
            c->tof_stale = false;
            o->events |= DRIVE_EVT_TOF_RESTORED;
        }
    } else {
        c->tof_stale = false;
    }
    bool fresh = in->enc.valid && (!c->enc_seen || in->enc.intervals != c->enc_intervals);
    float dt_enc_s = c->enc_seen && in->enc.t_us > c->enc_t_us ? (float)(in->enc.t_us - c->enc_t_us) / 1e6f : 0.02f;
    if (in->enc.valid) {
        c->enc_seen = true;
        c->enc_intervals = in->enc.intervals;
        c->enc_t_us = in->enc.t_us;
    }
    bool stale = enc_stale(in);
    bool regulated = false;
    // ARMED і OBSTACLE_HOLD — один рух (ADR-0004 phase6-tof): перехід між ними режим регулятора не перезахоплює й не скидає.
    if (driving && !c->armed_prev) {
        c->pid_mode = in->pid.mode;
        pid_reset_sides(c);
        pid_trace_acc_reset(&c->trace);
    }
    if (!driving) {
        pid_reset_sides(c);
    }
    c->armed_prev = driving;
    // Застарілий знімок у ARMED з увімкненим регулятором (spec AC-12b): подія один раз; оновлення — регулятор з нуля.
    if (driving && c->pid_mode == PID_MODE_ON) {
        if (stale && !c->enc_stale) {
            c->enc_stale = true;
            o->events |= DRIVE_EVT_ENC_STALE;
        } else if (!stale && c->enc_stale && in->enc.valid) {
            c->enc_stale = false;
            pid_reset_sides(c);
            o->events |= DRIVE_EVT_ENC_RESTORED;
        }
    } else {
        c->enc_stale = false;
    }

    update_link(c, in, o);

    if (!driving) {
        clear_window(c);  // поза рухом (ARMED, OBSTACLE_HOLD) виходи завжди вимкнені (AC-08)
    }

    bool usable = battery_snapshot_usable(&in->battery, in->now_ms);

    // Відсічка по напрузі під час руху (ARMED, OBSTACLE_HOLD) має перевагу над запитами (AC-14): знімок застарів, невалідний або напруга нижча за поріг.
    bool cutoff = false;
    if (driving) {
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
        o->failsafe_cause = APP_FS_VOLTAGE;
    }

    // Пульт у ARMED (конституція, п. 7): перший кадр закінчує консольне керування (роззброєння), а втрата звʼязку
    // понад 500 мс зупиняє ровер і веде у FAILSAFE. Відсічка по напрузі має перевагу над обома.
    bool stopped = cutoff;
    if (!cutoff && driving && c->remote_active) {
        if (o->events & DRIVE_EVT_REMOTE_APPEARED) {
            clear_window(c);
            o->stop = DRIVE_STOP_REMOTE_APPEARED;
            o->has_state_request = true;
            o->state_request = APP_STATE_DISARMED;
            stopped = true;
        } else if (!c->link_alive) {
            clear_window(c);
            o->stop = DRIVE_STOP_LINK_LOST;
            o->has_state_request = true;
            o->state_request = APP_STATE_FAILSAFE;
            o->failsafe_cause = APP_FS_LINK_LOST;
            stopped = true;
        }
    }

    switch (in->req.kind) {
    case DRIVE_REQ_ARM:
        if (in->state != APP_STATE_DISARMED) {
            o->reason = DRIVE_REASON_NOT_DISARMED;
        } else if (c->remote_active) {
            o->reason = DRIVE_REASON_REMOTE_ONLY;   // озброєння після появи пульта лише тумблером (AC-15)
        } else if (!usable) {
            o->reason = DRIVE_REASON_NO_MEASUREMENT;
        } else if (battery_is_below_cutoff(in->battery.mv)) {
            o->reason = DRIVE_REASON_LOW_VOLTAGE;
            o->mv = in->battery.mv;
        } else if (in->pid_saving) {
            o->reason = DRIVE_REASON_PID_SAVING;   // запис у флеш під час руху заборонений (phase5-pid AC-15)
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
        if (!stopped) {
            o->stop = DRIVE_STOP_DISARM;
            if (driving) {   // ARMED і OBSTACLE_HOLD: роззброєння за один перехід (пара HOLD → DISARMED)
                o->has_state_request = true;
                o->state_request = APP_STATE_DISARMED;
            }
        }
        break;
    case DRIVE_REQ_DRIVE:
        if (!driving) {
            o->reason = DRIVE_REASON_NOT_ARMED;
        } else if (c->remote_active) {
            o->reason = DRIVE_REASON_REMOTE_ONLY;   // рух після появи пульта лише стіком (AC-15)
        } else if (cutoff) {
            o->reason = usable ? DRIVE_REASON_LOW_VOLTAGE : DRIVE_REASON_NO_MEASUREMENT;
        } else if (!in_range(in->req.throttle) || !in_range(in->req.steer)) {
            o->reason = DRIVE_REASON_OUT_OF_RANGE;
        } else {
            float l, r;
            drive_mix(in->req.throttle, in->req.steer, &l, &r);
            float cl, cr;
            tof_limit_sides(l, r, c->occ_front, c->occ_rear, &cl, &cr);
            o->tof_clamped = (cl != l || cr != r);   // консоль один раз пояснить, що напрямок заблоковано перешкодою
            c->outputs = drive_outputs_from(l, r);
            c->left_permille = signed_pm(c->outputs.left);
            c->right_permille = signed_pm(c->outputs.right);
            c->win_mix_l = l;
            c->win_mix_r = r;
            c->window_active = true;
            c->accepted_ms = in->now_ms;
            c->min_mv = in->battery.mv;
            o->accepted = true;
        }
        break;
    case DRIVE_REQ_NONE:
        break;
    }

    // FAILSAFE через втрату звʼязку (T6, ADR-0002): відсічка по напрузі (те саме правило, що в ARMED) підвищує причину;
    // інакше після ≥ 1 с стабільного звʼязку з тумблером у DISARM — запит виходу в DISARMED.
    if (in->state == APP_STATE_FAILSAFE && in->failsafe_cause == APP_FS_LINK_LOST && c->remote_active) {
        bool cut = !usable || battery_is_below_cutoff(in->battery.mv);
        if (cut) {
            o->has_state_request = true;
            o->state_request = APP_STATE_FAILSAFE;
            o->failsafe_cause = APP_FS_VOLTAGE;
            o->mv = in->battery.mv;
            o->events |= DRIVE_EVT_CAUSE_UPGRADE;
        } else if (c->link_alive) {
            crsf_intent_t fit;
            crsf_intent(&in->rc, &fit);
            if (drive_failsafe_exit_state(c, in->failsafe_cause, &fit, in->now_ms) == DRIVE_FSX_READY) {
                o->has_state_request = true;
                o->state_request = APP_STATE_DISARMED;
                o->events |= DRIVE_EVT_FAILSAFE_EXIT;
            }
        }
    }

    // Пульт: намір пілота, фронт тумблера, озброєння, рух і роззброєння тумблером (ADR-0003). База тумблера
    // оновлюється на кожному такті за живого звʼязку, тож фронт буває рівно один раз.
    if (c->remote_active && c->link_alive) {
        crsf_intent_t it;
        crsf_intent(&in->rc, &it);
        bool edge = crsf_edge(&c->sw_base, it.sw);
        if (in->state == APP_STATE_DISARMED && edge && !o->has_state_request) {
            drive_block_t blk = arm_conditions(c, in->state, &in->battery, &it, in->now_ms);
            if (blk == DRIVE_BLOCK_NONE && in->pid_saving) {
                blk = DRIVE_BLOCK_PID_SAVING;   // phase5-pid AC-15
            }
            if (blk == DRIVE_BLOCK_NONE) {
                o->has_state_request = true;
                o->state_request = APP_STATE_ARMED;   // виходи лишаються вимкненими; STBY підніметься в ARMED
                o->events |= DRIVE_EVT_REMOTE_ARMED;
            } else {
                o->events |= DRIVE_EVT_ARM_REFUSED;
                o->arm_block = blk;
            }
        } else if (driving && !o->has_state_request && !stopped) {
            if (it.sw == CRSF_SW_DISARM) {
                clear_window(c);
                o->stop = DRIVE_STOP_SWITCH_DISARM;
                o->has_state_request = true;
                o->state_request = APP_STATE_DISARMED;
            } else {
                float l, r;
                drive_mix(it.throttle, it.steer, &l, &r);
                regulate(c, in, o, l, r, dt_s, fresh, dt_enc_s, stale);
                regulated = true;
            }
        }
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
        regulate(c, in, o, c->win_mix_l, c->win_mix_r, dt_s, fresh, dt_enc_s, stale);
        regulated = true;
        c->outputs = o->out;
        c->left_permille = o->left_permille;
        c->right_permille = o->right_permille;
        o->remaining_ms = DRIVE_WINDOW_MS - (uint32_t)(in->now_ms - c->accepted_ms);
        o->min_mv = c->min_mv;
    }

    // Утримання перед перешкодою (phase6-tof, ADR-0001): зайнятий сектор під час руху → OBSTACLE_HOLD, обидва вільні → назад в
    // ARMED. Відсічка, втрата звʼязку, пульт і роззброєння, що вже поставили запит, мають перевагу. Запити HOLD виходів
    // не вимикають: рух у вільний бік триває.
    if (!o->has_state_request) {
        if (in->state == APP_STATE_ARMED && (c->occ_front || c->occ_rear)) {
            const tof_view_sector_t *s = &in->tof.sector[c->occ_front ? TOF_SECTOR_FRONT : TOF_SECTOR_REAR];
            o->has_state_request = true;
            o->state_request = APP_STATE_OBSTACLE_HOLD;
            o->events |= DRIVE_EVT_HOLD_ENTER;
            o->hold_sector = c->occ_front ? TOF_SECTOR_FRONT : TOF_SECTOR_REAR;
            o->hold_mm = s->mm;
            o->hold_has_first = s->has_below;
            o->hold_first_ms = s->below_ms;
            o->hold_nearest = s->nearest;
        } else if (in->state == APP_STATE_OBSTACLE_HOLD && !c->occ_front && !c->occ_rear) {
            o->has_state_request = true;
            o->state_request = APP_STATE_ARMED;
            o->events |= DRIVE_EVT_HOLD_EXIT;
        }
    }

    // Без Task WDT (review F1): моторів не вмикаємо ні з пульта, ні з консолі. Запити зупинки (DISARMED, FAILSAFE)
    // лишаються — вони вже з вимкненими виходами.
    if (in->outputs_inhibited) {
        clear_window(c);
        o->out = drive_outputs_off();
        o->left_permille = 0;
        o->right_permille = 0;
        o->remaining_ms = 0;
        if (o->has_state_request && o->state_request == APP_STATE_ARMED) {
            o->has_state_request = false;
            o->events &= ~DRIVE_EVT_REMOTE_ARMED;
        }
        if (o->accepted && (in->req.kind == DRIVE_REQ_ARM || in->req.kind == DRIVE_REQ_DRIVE)) {
            o->accepted = false;
            o->reason = DRIVE_REASON_TRANSITION_REFUSED;
        }
        regulated = false;
    }

    // Такт без руху (нейтраль, зупинка, поза ARMED): стан регулятора скинуто, показ — лише режими й швидкості.
    if (!regulated) {
        pid_reset_sides(c);
        for (int b = 0; b < 2; b++) side_view(c, in, b, stale, &o->pid_view[b]);
    }
    if (driving) {
        o->trace_ready = pid_trace_acc_add(&c->trace, o->pid_view, in->now_ms, true, &o->trace);
    }
}

void drive_reason_text(drive_reason_t r, uint32_t mv, char *buf, size_t n)
{
    switch (r) {
    case DRIVE_REASON_NOT_DISARMED:
        snprintf(buf, n, "команда arm дозволена лише в стані DISARMED");
        break;
    case DRIVE_REASON_NOT_ARMED:
        snprintf(buf, n, "команда drive дозволена лише в станах ARMED і OBSTACLE_HOLD");
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
    case DRIVE_REASON_REMOTE_ONLY:
        snprintf(buf, n, "керує пульт; консоль — після перезапуску без нього");
        break;
    case DRIVE_REASON_PID_SAVING:
        snprintf(buf, n, "триває збереження набору регулятора — спробуйте ще раз за мить");
        break;
    case DRIVE_REASON_PID_MODE:
        snprintf(buf, n, "триває зміна режиму регулятора — спробуйте ще раз за мить");
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
    case DRIVE_STOP_LINK_LOST:      return "втрата звʼязку з пультом, FAILSAFE";
    case DRIVE_STOP_REMOTE_APPEARED:return "з'явився пульт: стенд закінчено, ровер роззброєно";
    case DRIVE_STOP_SWITCH_DISARM:  return "тумблер пульта в DISARM, ровер роззброєно";
    case DRIVE_STOP_NONE:           break;
    }
    return "немає";
}

void drive_disarm_text(app_state_t state_before, bool has_req, app_state_t req, drive_stop_t stop,
                       app_failsafe_cause_t cause, drive_fsx_t fsx, bool switch_arm, char *buf, size_t n)
{
    (void)cause;   // пояснення виходу з FAILSAFE вже враховує причину (fsx)
    if (stop == DRIVE_STOP_LOW_VOLTAGE || stop == DRIVE_STOP_STALE_VOLTAGE) {
        snprintf(buf, n, "спрацювала відсічка по напрузі, ровер у FAILSAFE");
    } else if (stop == DRIVE_STOP_LINK_LOST) {
        snprintf(buf, n, "втрата звʼязку з пультом, ровер у FAILSAFE");
    } else if (has_req && req == APP_STATE_DISARMED) {
        if (switch_arm) {
            snprintf(buf, n, "ровер роззброєно; тумблер пульта в ARM — для нового озброєння поверніть його в DISARM і знову в ARM");
        } else {
            snprintf(buf, n, "ровер роззброєно");
        }
    } else if (state_before == APP_STATE_FAILSAFE) {
        snprintf(buf, n, "стан FAILSAFE не змінено: %s", drive_fsx_text(fsx));
    } else {
        snprintf(buf, n, "стан %s не змінено: DISARMED досяжний лише з ARMED", app_state_name(state_before));
    }
}

void drive_arm_refused(drive_ctl_out_t *o, bool have_req, pid_busy_t busy)
{
    bool mode = busy == PID_BUSY_MODE;
    o->has_state_request = false;
    if (have_req && o->accepted) {
        o->accepted = false;
        o->reason = mode ? DRIVE_REASON_PID_MODE : DRIVE_REASON_PID_SAVING;
    }
    if (o->events & DRIVE_EVT_REMOTE_ARMED) {
        o->events &= ~DRIVE_EVT_REMOTE_ARMED;
        o->events |= DRIVE_EVT_ARM_REFUSED;
        o->arm_block = mode ? DRIVE_BLOCK_PID_MODE : DRIVE_BLOCK_PID_SAVING;
    }
}
