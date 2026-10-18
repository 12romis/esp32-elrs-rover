// HAL вимірювання напруги акумулятора: АЦП з калібруванням, задача battery, знімок (ADR-0003).
#pragma once

#include "battery_core.h"
#include "esp_err.h"

// Налаштовує АЦП (вхід PIN_VBAT_SENSE) і стартує задачу battery (core 0, пріоритет 2, 200 мс).
// Збій повертається як esp_err_t, без abort; знімок лишається невалідним.
esp_err_t battery_start(void);

// Копія останнього знімка напруги (під коротким критичним блоком).
void battery_get_snapshot(battery_snapshot_t *out);
