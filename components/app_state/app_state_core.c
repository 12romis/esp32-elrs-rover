// Автомат станів: таблиця переходів, запит переходу, кільце історії.
// Чиста логіка: без заголовків ESP-IDF і FreeRTOS, без блокувань (мʼютекс — у app_state_hal).
#include <string.h>

#include "app_state.h"

// Таблиця дозволених переходів [з][у]: рівно 10 пар із 49 (spec AC-06).
// Переходу в той самий стан у таблиці немає; виходів зі станів FAILSAFE і FAULT немає.
static const bool k_allowed[APP_STATE_COUNT][APP_STATE_COUNT] = {
    [APP_STATE_BOOT]          = {[APP_STATE_SELFTEST] = true},
    [APP_STATE_SELFTEST]      = {[APP_STATE_DISARMED] = true, [APP_STATE_FAULT] = true},
    [APP_STATE_DISARMED]      = {[APP_STATE_ARMED] = true, [APP_STATE_FAILSAFE] = true},
    [APP_STATE_ARMED]         = {[APP_STATE_DISARMED] = true, [APP_STATE_OBSTACLE_HOLD] = true,
                                 [APP_STATE_FAILSAFE] = true},
    [APP_STATE_OBSTACLE_HOLD] = {[APP_STATE_ARMED] = true, [APP_STATE_FAILSAFE] = true},
};

const char *app_state_name(app_state_t s)
{
    switch (s) {
    case APP_STATE_BOOT:          return "BOOT";
    case APP_STATE_SELFTEST:      return "SELFTEST";
    case APP_STATE_DISARMED:      return "DISARMED";
    case APP_STATE_ARMED:         return "ARMED";
    case APP_STATE_OBSTACLE_HOLD: return "OBSTACLE_HOLD";
    case APP_STATE_FAILSAFE:      return "FAILSAFE";
    case APP_STATE_FAULT:         return "FAULT";
    case APP_STATE_COUNT:         break;
    }
    return "?";
}

bool app_sm_is_allowed(app_state_t from, app_state_t to)
{
    if ((unsigned)from >= APP_STATE_COUNT || (unsigned)to >= APP_STATE_COUNT) {
        return false;
    }
    return k_allowed[from][to];
}

void app_sm_init(app_sm_t *sm, app_log_fn log)
{
    memset(sm, 0, sizeof(*sm));
    sm->state = APP_STATE_BOOT;
    sm->log = log;
}

// Дописує рядок s у буфер buf (розмір cap) після позиції *pos.
static void append(char *buf, size_t cap, size_t *pos, const char *s)
{
    size_t n = strlen(s);
    if (*pos + n >= cap) {
        n = cap - 1 - *pos;
    }
    memcpy(buf + *pos, s, n);
    *pos += n;
    buf[*pos] = '\0';
}

static void log_refusal(const app_sm_t *sm, app_state_t from, app_state_t to)
{
    if (sm->log == NULL) {
        return;
    }
    char msg[80];
    size_t pos = 0;
    msg[0] = '\0';
    append(msg, sizeof(msg), &pos, "перехід відхилено: ");
    append(msg, sizeof(msg), &pos, app_state_name(from));
    append(msg, sizeof(msg), &pos, " -> ");
    append(msg, sizeof(msg), &pos, app_state_name(to));
    sm->log(msg);
}

app_sm_result_t app_sm_request(app_sm_t *sm, app_state_t to, uint32_t t_ms)
{
    app_state_t from = sm->state;
    if (!app_sm_is_allowed(from, to)) {
        log_refusal(sm, from, to);
        return APP_SM_REFUSED;
    }

    size_t slot;
    if (sm->count < APP_SM_HISTORY_MAX) {
        slot = (sm->head + sm->count) % APP_SM_HISTORY_MAX;
        sm->count++;
    } else {
        slot = sm->head;  // кільце повне: перезаписуємо найстаріший
        sm->head = (sm->head + 1) % APP_SM_HISTORY_MAX;
    }
    sm->history[slot].from = from;
    sm->history[slot].to = to;
    sm->history[slot].t_ms = t_ms;
    sm->state = to;
    return APP_SM_OK;
}

app_state_t app_sm_state(const app_sm_t *sm)
{
    return sm->state;
}

size_t app_sm_history_count(const app_sm_t *sm)
{
    return sm->count;
}

bool app_sm_history_get(const app_sm_t *sm, size_t idx, app_sm_entry_t *out)
{
    if (idx >= sm->count) {
        return false;
    }
    *out = sm->history[(sm->head + idx) % APP_SM_HISTORY_MAX];
    return true;
}
