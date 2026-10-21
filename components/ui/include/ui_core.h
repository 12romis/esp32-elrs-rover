// Чиста логіка ui (spec AC-04…AC-23; ADR-0002, ADR-0004): задні ліхтарі, індикатор плати, дозволи команд світла й зумера,
// ручний beep, сигнали переходів, FAILSAFE-блимання й писк. Без заголовків ESP-IDF і FreeRTOS: час передається аргументом.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

// Діапазон зумера й тривалості автоматики (spec §6; межі входять).
#define UI_BEEP_FREQ_MIN_HZ 200
#define UI_BEEP_FREQ_MAX_HZ 5000
#define UI_BEEP_DUR_MIN_MS 1
#define UI_BEEP_DUR_MAX_MS 2000
#define UI_SIGNAL_TRANSITION_MS 100     // короткий сигнал переходу
#define UI_FAILSAFE_BEEP_MS 200         // писк у FAILSAFE
#define UI_FAILSAFE_PERIOD_MS 2000      // період писку, відлік від входу
#define UI_FAILSAFE_BLINK_HALF_MS 500   // півперіоду блимання задніх ліхтарів
#define UI_HOLD_BLINK_HALF_MS 250       // півперіод блимання в OBSTACLE_HOLD (phase7-telemetry A9); при кроці ui 100 мс — 200 або 300 мс
#define UI_DBL_BEEP_MS 100              // подвійний біп утримання (phase7-telemetry A5): звук
#define UI_DBL_GAP_MS 100               // пауза між двома звуками
#define UI_DBL_TOTAL_MS (2 * UI_DBL_BEEP_MS + UI_DBL_GAP_MS)

typedef enum { UI_CH_HEAD, UI_CH_RED, UI_CH_GREEN, UI_CH_COUNT } ui_channel_t;

// Результат команди (порядок перевірок: стан, канал × стан, доступність; синтаксис і діапазон — в console_core).
typedef enum {
    UI_OK,
    UI_DENY_STATE,       // стан не DISARMED і не ARMED
    UI_DENY_MOTION,      // red/green в ARMED: задні ліхтарі керує рух
    UI_DENY_EXPANDER,    // розширювач недоступний
    UI_DENY_RANGE        // beep поза діапазоном зумера
} ui_result_t;

typedef enum { UI_IND_OFF, UI_IND_RED, UI_IND_YELLOW } ui_indicator_t;   // жовтий — OBSTACLE_HOLD (phase7-telemetry A8)
typedef enum { UI_REAR_MANUAL, UI_REAR_AUTO } ui_rear_mode_t;

// Дозволи (єдина таблиця; console_core її викликає).
bool ui_core_state_allows(app_state_t st);                       // light і beep: лише DISARMED і ARMED
bool ui_core_channel_allows(app_state_t st, ui_channel_t ch);    // + red/green заборонені в ARMED
ui_rear_mode_t ui_core_rear_mode(app_state_t st);                // ARMED, OBSTACLE_HOLD і FAILSAFE — автоматика

typedef struct {
    uint32_t now_ms;
    app_state_t state;
    int16_t left_permille;    // застосовані значення бортів
    int16_t right_permille;
    bool expander_ok;         // розширювач доступний
    bool front_occupied;      // передній сектор зайнятий (прапорці control; блимання лише в OBSTACLE_HOLD)
    bool rear_occupied;       // задній сектор зайнятий
} ui_in_t;

typedef struct {
    bool head, red, green;            // бажані канали світла
    ui_indicator_t indicator;
    bool buzzer_on;                   // чи має звучати зараз
    uint32_t buzzer_hz;
} ui_out_t;

typedef struct {
    bool manual[UI_CH_COUNT];
    bool have_last;
    app_state_t last_state;
    bool failsafe;
    uint32_t fs_entry_ms;
    uint32_t hold_start_ms;           // вхід в OBSTACLE_HOLD: початок відліку фази блимання
    uint32_t fs_periods_done;         // скільки періодичних сигналів FAILSAFE вже подано
    uint32_t signal_hz;               // частота автосигналів; 0 — не задана (OQ-2), автосигналів немає
    struct {
        bool active;
        bool is_signal;               // автоматичний сигнал (переходу, FAILSAFE, подвійний біп)
        bool dbl;                     // подвійний біп: звук, пауза, звук від start_ms (dur_ms = UI_DBL_TOTAL_MS)
        uint32_t hz, start_ms, dur_ms;
    } buz;
    struct {
        bool pending;
        uint32_t hz, dur_ms;
    } slot;                           // один відкладений ручний beep на час сигналу
    ui_out_t out;                     // останній розрахований вихід
} ui_core_t;

void ui_core_init(ui_core_t *u, uint32_t signal_hz);

// Ручний light. UI_OK — бажаний стан каналу змінено (реальний запис виконує HAL).
ui_result_t ui_core_light(ui_core_t *u, ui_channel_t ch, bool on, app_state_t st, bool expander_ok);

// Ручний beep. UI_OK — звучить одразу, або (queued=true) лягло в слот і звучатиме після сигналу переходу.
ui_result_t ui_core_beep(ui_core_t *u, uint32_t hz, uint32_t dur_ms, uint32_t now_ms, app_state_t st, bool *queued);

// Відкат бажаного байта після невдалого запису ручної команди.
void ui_core_rollback(ui_core_t *u, ui_channel_t ch, bool previous);

// Такт 100 мс: переходи, сигнали, FAILSAFE, задні ліхтарі, індикатор. Результат — у u->out і в *out.
void ui_core_tick(ui_core_t *u, const ui_in_t *in, ui_out_t *out);

// Скільки мс лишилось звучати (0 — мовчить).
uint32_t ui_core_remaining_ms(const ui_core_t *u, uint32_t now_ms);
