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
    c->remote_active = false;
    c->link_alive = false;
    c->link_since_ms = 0;
    c->sw_base = CRSF_SW_UNKNOWN;
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
    }
    return "?";
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
    if ((int32_t)(now_ms - c->link_since_ms) < (int32_t)DRIVE_LINK_STABLE_MS) {
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
    bool alive = rc->have_frame && (int32_t)(in->now_ms - rc->t_ms) <= (int32_t)DRIVE_LINK_TIMEOUT_MS;
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

void drive_ctl_step(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *o)
{
    *o = (drive_ctl_out_t){0};
    o->out = drive_outputs_off();
    o->reason = DRIVE_REASON_NONE;

    update_link(c, in, o);

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
        o->failsafe_cause = APP_FS_VOLTAGE;
    }

    // Пульт у ARMED (конституція, п. 7): перший кадр закінчує консольне керування (роззброєння), а втрата звʼязку
    // понад 500 мс зупиняє ровер і веде у FAILSAFE. Відсічка по напрузі має перевагу над обома.
    bool stopped = cutoff;
    if (!cutoff && in->state == APP_STATE_ARMED && c->remote_active) {
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
            if (in->state == APP_STATE_ARMED) {
                o->has_state_request = true;
                o->state_request = APP_STATE_DISARMED;
            }
        }
        break;
    case DRIVE_REQ_DRIVE:
        if (in->state != APP_STATE_ARMED) {
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
            if (blk == DRIVE_BLOCK_NONE) {
                o->has_state_request = true;
                o->state_request = APP_STATE_ARMED;   // виходи лишаються вимкненими; STBY підніметься в ARMED
                o->events |= DRIVE_EVT_REMOTE_ARMED;
            } else {
                o->events |= DRIVE_EVT_ARM_REFUSED;
                o->arm_block = blk;
            }
        } else if (in->state == APP_STATE_ARMED && !o->has_state_request && !stopped) {
            if (it.sw == CRSF_SW_DISARM) {
                clear_window(c);
                o->stop = DRIVE_STOP_SWITCH_DISARM;
                o->has_state_request = true;
                o->state_request = APP_STATE_DISARMED;
            } else {
                float l, r;
                drive_mix(it.throttle, it.steer, &l, &r);
                o->out = drive_outputs_from(l, r);
                o->left_permille = signed_pm(o->out.left);
                o->right_permille = signed_pm(o->out.right);
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
    case DRIVE_REASON_REMOTE_ONLY:
        snprintf(buf, n, "керує пульт; консоль — після перезапуску без нього");
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


