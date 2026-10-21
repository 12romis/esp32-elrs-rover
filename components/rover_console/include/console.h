// HAL консолі: задача console, ввід-вивід через USB-Serial-JTAG, виконання команд.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pid_core.h"
#include "ui_core.h"

typedef enum { CONSOLE_MOTOR_ARM, CONSOLE_MOTOR_DISARM, CONSOLE_MOTOR_DRIVE } console_motor_cmd_t;

typedef struct {
    bool accepted;
    int16_t left_permille;    // застосовані значення бортів зі знаком
    int16_t right_permille;
    char text[192];           // пояснення відмови (або повідомлення), українською; найдовше — пояснення disarm у FAILSAFE
} console_motor_reply_t;

typedef struct {
    uint32_t mv;
    bool mv_valid;
    int stby;
    int16_t left_permille;
    int16_t right_permille;
    uint32_t remaining_ms;
    const char *last_stop_reason;  // NULL — зупинок ще не було
    uint32_t last_stop_min_mv;     // 0 — невідома
    uint32_t last_stop_t_ms;
} console_motor_status_t;

// Відповідь ui на light чи beep (spec AC-04…AC-10, AC-14, AC-15).
typedef struct {
    ui_result_t result;       // UI_OK — прийнято; інакше причина відмови
    bool bus_failure;         // запис не підтверджено: ручна зміна відкочена
    bool queued;              // beep ліг у слот і звучатиме після сигналу переходу
    bool head, red, green;    // стан каналів світла після команди
} console_ui_reply_t;

typedef struct {
    bool expander_ok;
    bool head, red, green;
    bool rear_auto;
    bool indicator_red;
    bool buzzer_on;
    uint32_t remaining_ms;
} console_ui_status_t;

// Дії від інших модулів передає main — консоль не залежить від watchdog і storage (ADR-0002).
typedef struct {
    void (*storage_status)(char *buf, size_t n);  // текст стану сховища у буфер виклику (з причиною, якщо недоступне)
    void (*request_hang)(void);           // запит навмисного зависання (wdt-test)
    const char *(*reset_reason)(void);    // коротка назва причини останнього скидання
    void (*reboot)(void);                 // перезавантаження; NULL → esp_restart()
    // Команди моторів: false — керування не відповіло. NULL → команда відхиляється з поясненням.
    bool (*motor_cmd)(console_motor_cmd_t cmd, float throttle, float steer, console_motor_reply_t *reply);
    void (*motor_status)(console_motor_status_t *s);
    // Світло й зумер: false — ui не стартувала. NULL → команда відхиляється з поясненням.
    bool (*light_cmd)(ui_channel_t ch, bool on, console_ui_reply_t *reply);
    bool (*beep_cmd)(uint32_t hz, uint32_t dur_ms, console_ui_reply_t *reply);
    void (*ui_status)(console_ui_status_t *s);
    // Рядки status про пульт (багаторядковий текст, рядки розділені \n); повертає довжину. NULL — рядків немає.
    size_t (*rc_status)(char *buf, size_t n);
    // Енкодери (phase4-encoders, contracts/cli.md): знімок, обнулення з відповіддю, рядок status. Пишуть текст у буфер
    // виклику (рядки розділені \n) і повертають довжину. NULL — енкодери не підключені.
    size_t (*enc_snapshot)(char *buf, size_t n);
    size_t (*enc_reset)(char *buf, size_t n);
    size_t (*enc_status)(char *buf, size_t n);
    // Регулятор швидкості (phase5-pid, spec AC-03…AC-08, AC-15). Пишуть відповідь у буфер виклику (рядки розділені \n)
    // і повертають довжину. NULL — регулятор не підключений.
    size_t (*pid_show)(char *buf, size_t n);
    size_t (*pid_set_param)(pid_param_t p, float value, char *buf, size_t n);
    size_t (*pid_set_mode)(bool on, char *buf, size_t n);
    size_t (*pid_save)(char *buf, size_t n);
    void (*pid_trace)(bool on);
    // Далекомірі (phase6-tof): показ і повторна ініціалізація пишуть відповідь у буфер виклику (рядки розділені \n) і
    // повертають довжину. NULL — далекоміри не підключені.
    size_t (*tof_show)(char *buf, size_t n);
    size_t (*tof_reinit)(char *buf, size_t n);
} console_ops_t;

// Встановлює драйвер USB-Serial-JTAG і стартує задачу console (core 0, пріоритет 1).
// ops має жити весь час роботи. Збій повертається як esp_err_t, без abort.
esp_err_t console_start(const console_ops_t *ops);
