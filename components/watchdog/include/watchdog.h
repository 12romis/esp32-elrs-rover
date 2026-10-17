// Політика годування сторожового таймера (spec AC-04, ADR-0001). Чиста логіка: без заголовків ESP-IDF.
#pragma once

#include <stdbool.h>

typedef struct {
    bool hang_requested;
} watchdog_core_t;

void watchdog_core_init(watchdog_core_t *w);

// Команда навмисного зависання: після неї годувати не можна.
void watchdog_core_request_hang(watchdog_core_t *w);

// Чи годувати сторожовий таймер у цьому циклі heartbeat.
bool watchdog_should_feed(const watchdog_core_t *w);
