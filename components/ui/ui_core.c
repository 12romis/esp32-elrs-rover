// Чиста логіка ui (T3, T4; spec AC-04…AC-23; ADR-0002, ADR-0004). Час — аргументом, без ESP-IDF і FreeRTOS.
#include "ui_core.h"

#include <string.h>

// ---------- дозволи ----------

bool ui_core_state_allows(app_state_t st)
{
    return st == APP_STATE_DISARMED || st == APP_STATE_ARMED;
}

bool ui_core_channel_allows(app_state_t st, ui_channel_t ch)
{
    if (!ui_core_state_allows(st)) {
        return false;
    }
    if (st == APP_STATE_ARMED && (ch == UI_CH_RED || ch == UI_CH_GREEN)) {
        return false;  // в ARMED задні ліхтарі керує рух
    }
    return true;
}

ui_rear_mode_t ui_core_rear_mode(app_state_t st)
{
    return (app_state_is_driving(st) || st == APP_STATE_FAILSAFE) ? UI_REAR_AUTO : UI_REAR_MANUAL;
}

void ui_core_init(ui_core_t *u, uint32_t signal_hz)
{
    if (u == NULL) {
        return;
    }
    memset(u, 0, sizeof(*u));
    u->signal_hz = signal_hz;
}

// ---------- ручні команди ----------

ui_result_t ui_core_light(ui_core_t *u, ui_channel_t ch, bool on, app_state_t st, bool expander_ok)
{
    if (u == NULL || ch < 0 || ch >= UI_CH_COUNT) {
        return UI_DENY_STATE;
    }
    if (!ui_core_state_allows(st)) {
        return UI_DENY_STATE;
    }
    if (!ui_core_channel_allows(st, ch)) {
        return UI_DENY_MOTION;
    }
    if (!expander_ok) {
        return UI_DENY_EXPANDER;
    }
    u->manual[ch] = on;
    return UI_OK;
}

void ui_core_rollback(ui_core_t *u, ui_channel_t ch, bool previous)
{
    if (u != NULL && ch >= 0 && ch < UI_CH_COUNT) {
        u->manual[ch] = previous;
    }
}

static bool sounding(const ui_core_t *u, uint32_t now_ms)
{
    return u->buz.active && (uint32_t)(now_ms - u->buz.start_ms) < u->buz.dur_ms;
}

// Чи зараз іде звук (у подвійному біпі між двома звуками пауза).
static bool sound_on(const ui_core_t *u, uint32_t now_ms)
{
    if (!sounding(u, now_ms)) {
        return false;
    }
    if (u->buz.dbl) {
        // Сегменти звук / пауза / звук з допуском півкроку: такт ui може зсунутися на кілька мс (sensors витісняє ui).
        uint32_t seg = ((uint32_t)(now_ms - u->buz.start_ms) + UI_DBL_BEEP_MS / 2u) / UI_DBL_BEEP_MS;
        return seg == 0 || seg == 2;
    }
    return true;
}

static void start_sound(ui_core_t *u, uint32_t hz, uint32_t dur_ms, uint32_t now_ms, bool is_signal)
{
    u->buz.active = true;
    u->buz.is_signal = is_signal;
    u->buz.dbl = false;
    u->buz.hz = hz;
    u->buz.start_ms = now_ms;
    u->buz.dur_ms = dur_ms;
    u->out.buzzer_on = true;
    u->out.buzzer_hz = hz;
}

ui_result_t ui_core_beep(ui_core_t *u, uint32_t hz, uint32_t dur_ms, uint32_t now_ms, app_state_t st, bool *queued)
{
    if (queued != NULL) {
        *queued = false;
    }
    if (u == NULL || !ui_core_state_allows(st)) {
        return UI_DENY_STATE;
    }
    if (hz < UI_BEEP_FREQ_MIN_HZ || hz > UI_BEEP_FREQ_MAX_HZ || dur_ms < UI_BEEP_DUR_MIN_MS ||
        dur_ms > UI_BEEP_DUR_MAX_MS) {
        return UI_DENY_RANGE;
    }
    if (sounding(u, now_ms) && u->buz.is_signal) {
        u->slot.pending = true;  // один слот: наступна команда перезаписує попередню
        u->slot.hz = hz;
        u->slot.dur_ms = dur_ms;
        if (queued != NULL) {
            *queued = true;
        }
        return UI_OK;
    }
    start_sound(u, hz, dur_ms, now_ms, false);  // замінює звучання, що триває
    return UI_OK;
}

uint32_t ui_core_remaining_ms(const ui_core_t *u, uint32_t now_ms)
{
    if (u == NULL || !sounding(u, now_ms)) {
        return 0;
    }
    return u->buz.dur_ms - (uint32_t)(now_ms - u->buz.start_ms);
}

// ---------- такт ----------

// Подвійний біп (A5, A7): не стартує, поки звучить попередній; сигнали DISARMED і FAILSAFE замінюють його (start_sound).
static void start_double(ui_core_t *u, uint32_t now_ms)
{
    if (u->buz.active && u->buz.dbl && sounding(u, now_ms)) {
        return;
    }
    start_sound(u, u->signal_hz, UI_DBL_TOTAL_MS, now_ms, true);
    u->buz.dbl = true;
}

// Одинарний сигнал переходу: DISARMED, FAILSAFE і вхід у рух зі стану не-руху. Входи в OBSTACLE_HOLD і вихід у ARMED
// обробляє подвійний біп (ADR-0002 phase7-telemetry, що замінює «тихий» перехід з ADR-0004 phase6-tof); тут лишається
// озброєння з DISARMED в ARMED. «Вхід у рух» — за станом, який бачить ui (опитування раз на 100 мс).
static bool is_signal_state(app_state_t s, bool entering_motion)
{
    return s == APP_STATE_DISARMED || s == APP_STATE_FAILSAFE || entering_motion;
}

void ui_core_tick(ui_core_t *u, const ui_in_t *in, ui_out_t *out)
{
    if (u == NULL || in == NULL) {
        return;
    }
    bool changed = !u->have_last || in->state != u->last_state;
    bool was_hold = u->have_last && u->last_state == APP_STATE_OBSTACLE_HOLD;
    bool driving = app_state_is_driving(in->state);
    bool entering_motion = changed && driving && !(u->have_last && app_state_is_driving(u->last_state));
    if (changed) {
        if (entering_motion) {
            u->manual[UI_CH_HEAD] = true;   // на вході в рух фари вмикаються самі; light head off в ARMED ще можна
        }
        if (driving || in->state == APP_STATE_FAILSAFE) {
            u->manual[UI_CH_RED] = false;   // далі задні ліхтарі керує автоматика, ручний стан не повертається
            u->manual[UI_CH_GREEN] = false;
        }
        if (in->state == APP_STATE_FAILSAFE) {
            u->manual[UI_CH_HEAD] = false;  // при виході з ARMED фари гаснуть
        }
        // Відлік FAILSAFE (блимання, періодичний писк) живе лише у FAILSAFE; з фази 3 з нього є вихід у DISARMED
        // (ADR-0002 phase3-crsf), тож прапорець скидається при будь-якій зміні стану на інший.
        if (in->state == APP_STATE_OBSTACLE_HOLD) {
            u->hold_start_ms = in->now_ms;   // одна фаза блимання на обидва боки
        }
        u->failsafe = (in->state == APP_STATE_FAILSAFE);
        if (u->failsafe) {
            u->fs_entry_ms = in->now_ms;
            u->fs_periods_done = 0;
        }
        if (in->state == APP_STATE_DISARMED) {
            u->manual[UI_CH_HEAD] = false;  // у DISARMED фари вмикаються лише вручну
            u->manual[UI_CH_RED] = true;    // задні ліхтарі в DISARMED за замовчуванням червоні, далі їх можна змінити
            u->manual[UI_CH_GREEN] = false;
        }
        if (!ui_core_state_allows(in->state)) {
            u->slot.pending = false;  // відкладений ручний beep не лунає в станах, де ручний звук заборонений (FAILSAFE тощо)
        }
        u->last_state = in->state;
        u->have_last = true;
    }

    // Автоматичні звуки спершу (вони замінюють усе, що звучить), потім завершення звуку й слот.
    if (u->signal_hz != 0) {
        if (changed && in->state == APP_STATE_OBSTACLE_HOLD) {
            start_double(u, in->now_ms);    // вхід в утримання (зокрема видимий з DISARMED) замість сигналу озброєння
        } else if (changed && was_hold && in->state == APP_STATE_ARMED) {
            start_double(u, in->now_ms);    // вихід в ARMED; в DISARMED і FAILSAFE лунають їхні власні сигнали
        } else if (changed && is_signal_state(in->state, entering_motion)) {
            start_sound(u, u->signal_hz, UI_SIGNAL_TRANSITION_MS, in->now_ms, true);
        }
        if (u->failsafe) {
            uint32_t periods = (uint32_t)(in->now_ms - u->fs_entry_ms) / UI_FAILSAFE_PERIOD_MS;
            if (periods > u->fs_periods_done) {
                u->fs_periods_done = periods;
                start_sound(u, u->signal_hz, UI_FAILSAFE_BEEP_MS, in->now_ms, true);
            }
        }
    }
    if (u->buz.active && !sounding(u, in->now_ms)) {
        u->buz.active = false;
        if (u->slot.pending) {
            u->slot.pending = false;
            start_sound(u, u->slot.hz, u->slot.dur_ms, in->now_ms, false);
        }
    }

    ui_out_t o;
    memset(&o, 0, sizeof(o));
    o.head = u->manual[UI_CH_HEAD];  // фари завжди ручні
    if (in->expander_ok) {
        if (driving) {
            int sum = (int)in->left_permille + (int)in->right_permille;
            o.red = sum >= 0;   // жовтий (червоний і зелений разом), доки стоїть чи їде вперед
            o.green = true;     // зелений світить завжди; назад — лише зелений
        } else if (in->state == APP_STATE_FAILSAFE) {
            uint32_t phase = (in->now_ms - u->fs_entry_ms) / UI_FAILSAFE_BLINK_HALF_MS;
            o.red = (phase % 2u) == 0u;  // першою фазою є «світить»
        } else {
            o.red = u->manual[UI_CH_RED];
            o.green = u->manual[UI_CH_GREEN];
        }
    }
    if (in->state == APP_STATE_OBSTACLE_HOLD && in->expander_ok) {
        // Попередження про бік перешкоди (A9–A11): блимання перекриває вихід, ручний стан фар не змінюється й повертається сам.
        bool phase_on = (((uint32_t)(in->now_ms - u->hold_start_ms)) / UI_HOLD_BLINK_HALF_MS) % 2u == 0u;
        if (in->front_occupied) {
            o.head = phase_on;
        }
        if (in->rear_occupied) {
            o.red = o.red && phase_on;
            o.green = o.green && phase_on;
        }
    }
    o.indicator = in->state == APP_STATE_FAILSAFE ? UI_IND_RED : (in->state == APP_STATE_OBSTACLE_HOLD ? UI_IND_YELLOW : UI_IND_OFF);
    bool on = u->buz.active && sound_on(u, in->now_ms);
    o.buzzer_on = on;
    o.buzzer_hz = on ? u->buz.hz : 0;
    u->out = o;
    if (out != NULL) {
        *out = o;
    }
}
