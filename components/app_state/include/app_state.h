// Автомат станів ровера (docs/02-FIRMWARE-CONTEXT.md §6.3). Чиста логіка: без заголовків ESP-IDF.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    APP_STATE_BOOT,
    APP_STATE_SELFTEST,
    APP_STATE_DISARMED,
    APP_STATE_ARMED,
    APP_STATE_OBSTACLE_HOLD,
    APP_STATE_FAILSAFE,
    APP_STATE_FAULT,
    APP_STATE_COUNT
} app_state_t;

typedef enum {
    APP_SM_OK,
    APP_SM_REFUSED
} app_sm_result_t;

// Колбек запису відмов (на платі — журнал, на хості — перехоплення тексту).
typedef void (*app_log_fn)(const char *msg);

#define APP_SM_HISTORY_MAX 16

typedef struct {
    app_state_t from;
    app_state_t to;
    uint32_t t_ms;
} app_sm_entry_t;

// Причина FAILSAFE (ADR-0002): від неї залежить, чи можна вийти без апаратного скидання.
typedef enum {
    APP_FS_NONE,        // ровер не у FAILSAFE
    APP_FS_LINK_LOST,   // втрата зв'язку з пультом: вихід у DISARMED дозволено за умов керування
    APP_FS_VOLTAGE      // відсічка по напрузі: вихід лише апаратним скиданням
} app_failsafe_cause_t;

typedef struct {
    app_state_t state;
    app_failsafe_cause_t failsafe_cause;
    app_log_fn log;
    app_sm_entry_t history[APP_SM_HISTORY_MAX];
    size_t head;   // індекс найстаршого запису
    size_t count;  // скільки записів зараз у кільці
} app_sm_t;

const char *app_state_name(app_state_t s);

// Чи є пара from->to у таблиці дозволених переходів.
bool app_sm_is_allowed(app_state_t from, app_state_t to);

void app_sm_init(app_sm_t *sm, app_log_fn log);

// Запит переходу. Дозволений — стан змінюється і запис іде в історію.
// Відхилений — стан не змінюється, log отримує текст з іменами обох станів.
app_sm_result_t app_sm_request(app_sm_t *sm, app_state_t to, uint32_t t_ms);

// Вхід у FAILSAFE з причиною (ADR-0002). У FAILSAFE повторне повідомлення з причиною VOLTAGE підвищує причину
// назавжди (без запису в історію), а LINK_LOST її не знижує. З недозволеного стану — відмова.
app_sm_result_t app_sm_enter_failsafe(app_sm_t *sm, app_failsafe_cause_t cause, uint32_t t_ms);

app_failsafe_cause_t app_sm_failsafe_cause(const app_sm_t *sm);
const char *app_failsafe_cause_name(app_failsafe_cause_t c);

app_state_t app_sm_state(const app_sm_t *sm);
size_t app_sm_history_count(const app_sm_t *sm);
// idx 0 — найстаріший запис. Повертає false, якщо idx поза межами.
bool app_sm_history_get(const app_sm_t *sm, size_t idx, app_sm_entry_t *out);
