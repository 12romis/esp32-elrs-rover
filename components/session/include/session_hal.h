// HAL session (phase8-session-report, ADR-0001): виклик session_core з кроку керування та черга підсумків між задачею
// control (core 1) і задачею report (core 0). Без мʼютексів у гарячому шляху: черга FreeRTOS довжини 2, нові підсумки
// не чекають (повна черга — найстаріший затирається).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"
#include "battery_core.h"
#include "encoders_core.h"
#include "esp_err.h"
#include "session_core.h"

// Створює чергу підсумків. Викликається до старту control.
esp_err_t session_hal_init(void);

// Вхід кроку керування для сесії: усе, що знає control після drive_ctl_step.
typedef struct {
    uint32_t now_ms;
    app_state_t state;
    app_failsafe_cause_t failsafe_cause;
    battery_snapshot_t battery;
    bool tof_control_on;
    bool hold_enter;
    uint16_t hold_mm;
    int8_t hold_nearest;
    uint8_t hold_below_mask;
    int16_t left_permille, right_permille;
    float cmd_left, cmd_right;
} session_hal_in_t;

// Один виклик із задачі control наприкінці кроку (до скидання сторожового таймера). Лічильники енкодерів дають пробіг.
// Завершений підсумок кладеться в чергу без очікування.
void session_hal_step(const session_hal_in_t *in, const enc_state_t *enc);

// Наступний номер сесії піднімається до значення зі сховища (ніколи не зменшується): викликає report після відновлення.
void session_hal_raise_next_seq(uint32_t next_seq);

// Наступний номер сесії (для показу).
uint32_t session_hal_next_seq(void);

// Очікує підсумок до timeout_ms (0 — без очікування). true — *out заповнено. Викликає лише задача report.
bool session_hal_receive(session_summary_t *out, uint32_t timeout_ms);

// Сесія триває (для status).
bool session_hal_active(void);
