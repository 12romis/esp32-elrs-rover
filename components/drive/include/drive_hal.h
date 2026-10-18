// HAL руху: LEDC і GPIO моторів, задача control (20 мс), черга запитів, Task WDT (ADR-0001, ADR-0004).
// Рішення приймає drive_core; тут лише виконання. Лінії моторів пише тільки цей модуль.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "drive_core.h"
#include "esp_err.h"

typedef struct {
    int stby;                 // фактичний рівень, поданий на лінію STBY
    int16_t left_permille;    // застосовані значення бортів зі знаком
    int16_t right_permille;
    uint32_t remaining_ms;    // скільки лишилось діяти команді руху
    drive_stop_t last_stop;   // остання зупинка моторів (DRIVE_STOP_NONE — зупинок ще не було)
    uint32_t last_stop_min_mv;  // мінімальна напруга за час руху (0 — невідома)
    uint32_t last_stop_t_ms;  // час зупинки, мс від старту
    // --- phase3-crsf: стан керування з пульта для status (знімок останнього такту control) ---
    bool remote_active;       // стендовий режим закінчено
    bool link_alive;
    uint32_t since_last_ms;   // час від останнього кадру каналів (лише якщо remote_active)
    crsf_intent_t intent;     // тяга, поворот, тумблер після мертвої зони
    drive_block_t arm_block;  // що блокує озброєння з пульта зараз (DRIVE_BLOCK_NONE — можна)
    app_failsafe_cause_t failsafe_cause;
    drive_fsx_t fsx;          // чого бракує для виходу з FAILSAFE
} drive_status_t;

typedef struct {
    bool accepted;
    drive_reason_t reason;
    uint32_t mv;              // виміряна напруга для пояснення відмови
    int16_t left_permille;
    int16_t right_permille;
    app_state_t state_before;   // стан на початку такту
    bool has_state_request;     // control запитав перехід
    app_state_t state_request;
    drive_stop_t stop;
    app_failsafe_cause_t failsafe_cause;  // причина поточного FAILSAFE (для пояснення disarm)
    drive_fsx_t fsx;                      // чого бракує для виходу з FAILSAFE
    bool switch_arm;                      // тумблер пульта в ARM
} drive_reply_t;

// Лінії моторів → вимкнений стан (STBY низька, ШІМ 0, DIR 0/0). Перший виклик після app_state_hal_init.
// Збій повертається як esp_err_t; виходи при цьому лишаються вимкненими.
esp_err_t drive_hal_init(void);

// Стартує задачу control (core 1, пріоритет 5, 20 мс) і підписує її на Task WDT.
esp_err_t drive_start(void);

// Подає запит у чергу і чекає відповіді до 200 мс. false — control не відповіла (або не стартувала).
bool drive_hal_submit(const drive_req_t *req, drive_reply_t *reply);

void drive_hal_status(drive_status_t *s);

// true, якщо drive_hal_init і drive_start пройшли успішно (інакше моторів ніколи не вмикали).
bool drive_hal_running(void);
