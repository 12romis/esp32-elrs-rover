// HAL автомата станів: мʼютекс m_state, час, журнал. Логіка — в app_state_core.c.
#pragma once

#include <stddef.h>

#include "app_state.h"
#include "esp_err.h"

// Створює мʼютекс і автомат у стані BOOT; друкує стан в журнал.
esp_err_t app_state_hal_init(void);

// Запит переходу під мʼютексом. Успіх і відмова друкуються в журнал з іменами станів.
app_sm_result_t app_state_request(app_state_t to);

app_state_t app_state_get(void);

// Копія історії переходів (від найстарішого). Повертає кількість записів.
size_t app_state_history(app_sm_entry_t *out, size_t max);
