// HAL енкодерів коліс (ADR-0001, ADR-0002): чотири блоки лічильника імпульсів, задача encoders (20 мс), стан під portMUX.
// Логіка накопичення, швидкості й текстів — в encoders_core.c. Керування енкодери не торкаються.
#pragma once

#include <stddef.h>

#include "encoders_core.h"
#include "esp_err.h"

// Налаштовує блоки лічильника (лінії з board_pins.h) і стартує задачу encoders (core 1, пріоритет 4).
// Збій налаштування колеса робить недоступним лише його; збій створення задачі — усі колеса недоступні.
// Помилка повертається для журналу, але ровер стартує далі (spec AC-08).
esp_err_t encoders_start(void);

// Копія стану енкодерів під коротким блокуванням (у фазі 5 — для керування).
void encoders_state_get(enc_state_t *out);

// Тексти для консолі (contracts/cli.md); повертають довжину записаного тексту.
size_t encoders_snapshot_text(char *buf, size_t n);
size_t encoders_reset_text(char *buf, size_t n);   // обнуляє доступні колеса й описує результат
size_t encoders_status_text(char *buf, size_t n);
