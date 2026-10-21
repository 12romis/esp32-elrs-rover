// HAL ui (ADR-0002, ADR-0004): задача ui (100 мс), мʼютекс m_ui, зумер на LEDC, порт індикатора плати.
// Рішення приймає ui_core; тут лише виконання. Після старту єдиний, хто пише в розширювач, зумер і індикатор.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ui_core.h"

typedef struct {
    ui_result_t result;     // UI_OK — прийнято
    bool bus_failure;       // запис не підтверджено шиною: ручна зміна відкочена, стан світла не змінено
    bool queued;            // beep ліг у слот і звучатиме після сигналу переходу
    bool head, red, green;  // стан каналів світла після команди
} ui_hal_reply_t;

typedef struct {
    bool expander_ok;
    bool head, red, green;
    bool rear_auto;         // задні ліхтарі керує автоматика (ARMED, FAILSAFE)
    ui_indicator_t indicator;   // колір індикатора плати: погашений, червоний, жовтий
    bool buzzer_on;
    uint32_t remaining_ms;
} ui_hal_status_t;

// Налаштовує зумер (LEDC таймер 1, канал 2, спокій — низький рівень), мʼютекс, стартує задачу ui (core 0, пріоритет 2, 100 мс).
esp_err_t ui_start(void);
bool ui_hal_running(void);

// Ручні команди виконуються в контексті викликача під мʼютексом m_ui.
void ui_hal_light(ui_channel_t ch, bool on, ui_hal_reply_t *reply);
void ui_hal_beep(uint32_t hz, uint32_t dur_ms, ui_hal_reply_t *reply);
void ui_hal_status(ui_hal_status_t *s);
