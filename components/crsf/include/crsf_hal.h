// HAL приймача CRSF (ADR-0001, ADR-0003): UART2, задача crsf_rx, знімок для керування й консолі.
// Рішень про безпеку тут немає: задача лише збирає кадри (crsf_core) і публікує знімок.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "crsf_core.h"
#include "esp_err.h"

// Налаштовує UART2 (420000 8N1, RX/TX з board_pins.h) і стартує задачу crsf_rx (core 1, пріоритет 6).
// Помилка — ровер лишається в стендовому режимі (кадрів не буде), про це йде рядок у журнал.
esp_err_t crsf_start(void);
bool crsf_hal_running(void);

// Записує готовий кадр у UART2 (phase7-telemetry, ADR-0001): єдиний власник лінії — цей модуль. false — драйвер не встановлено,
// кадр порожній чи завеликий, або запис не вдався; керування від результату не залежить. Блокує лише на запис кількох байтів.
bool crsf_hal_send(const uint8_t *frame, size_t len);

// Копія знімка під коротким блокуванням. До старту або без кадрів — нульовий знімок (have_frame = false).
void crsf_snapshot_get(crsf_snapshot_t *out);
