// HAL telemetry (phase7-telemetry, ADR-0001, ADR-0003): задача telemetry (200 мс) передає на пульт кадр датчика батареї через
// crsf_hal_send; статистика часу кроку керування. Рішень про безпеку тут немає: збій передачі ні на що не впливає.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "telemetry_core.h"

// Стартує задачу telemetry (core 0, пріоритет 2, 200 мс). Викликається після battery_start і crsf_start. Збій — ESP_FAIL/код,
// ровер працює без телеметрії.
esp_err_t telemetry_start(void);

// Запис виміру кроку керування (з задачі control, одразу перед скиданням сторожового таймера): короткий критичний блок.
void telemetry_step_record(uint32_t us, uint32_t period_us);

// Копія статистики кроку під коротким блоком (для status).
void telemetry_step_get(tel_step_stats_t *out);

// Рядок status про крок керування (з періодом, переданим під час запису); повертає довжину.
size_t telemetry_step_text(char *buf, size_t n);
