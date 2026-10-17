// HAL консолі: задача console, ввід-вивід через USB-Serial-JTAG, виконання команд.
#pragma once

#include "esp_err.h"

// Дії від інших модулів передає main — консоль не залежить від watchdog і storage (ADR-0002).
typedef struct {
    const char *(*storage_status)(void);  // текст стану сховища (з причиною, якщо недоступне)
    void (*request_hang)(void);           // запит навмисного зависання (wdt-test)
    const char *(*reset_reason)(void);    // коротка назва причини останнього скидання
    void (*reboot)(void);                 // перезавантаження; NULL → esp_restart()
} console_ops_t;

// Встановлює драйвер USB-Serial-JTAG і стартує задачу console (core 0, пріоритет 1).
// ops має жити весь час роботи. Збій повертається як esp_err_t, без abort.
esp_err_t console_start(const console_ops_t *ops);
