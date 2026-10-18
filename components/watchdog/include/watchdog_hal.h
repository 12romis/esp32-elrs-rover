// HAL сторожового таймера задач: panic-режим, задача heartbeat, причина скидання (ADR-0001).
#pragma once

#include "esp_err.h"

// Вмикає panic для Task WDT програмно (таймаут і ядра — з Kconfig) і стартує задачу heartbeat.
// Збій повертається як esp_err_t, без abort.
esp_err_t watchdog_start(void);

// Команда навмисного зависання: heartbeat перестає годувати сторожовий таймер.
void watchdog_request_hang(void);

// Коротка назва причини останнього скидання (POWERON, SW, TASK_WDT, PANIC, ...).
const char *watchdog_last_reset_reason(void);
