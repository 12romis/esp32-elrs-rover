// HAL сховища налаштувань: nvs_flash за рішенням storage_core.
#pragma once

#include "esp_err.h"
#include "storage.h"

// Ініціалізує NVS. Очищає лише за «немає вільних сторінок» або «знайдено новішу версію».
// Не викликає abort: збій повертається як STORAGE_STATUS_UNAVAILABLE (ADR-0004).
storage_status_t storage_init(void);

// Остання помилка nvs_flash (ESP_OK, якщо збою не було).
esp_err_t storage_last_error(void);
