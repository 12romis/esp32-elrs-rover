// HAL шини периферії (ADR-0001): шина i2c_master 100 кГц і мʼютекс m_i2c для всіх користувачів (ioexp, у фазі 6 tof).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

// Створює шину 100 кГц (внутрішні підтяжки вимкнено: їх дає модуль розширювача) і мʼютекс. Повторний виклик — ESP_OK.
esp_err_t i2c_bus_init(void);

i2c_master_bus_handle_t i2c_bus_handle(void);

// Мʼютекс шини: одночасно виконується рівно одна транзакція. timeout_ms — скільки чекати на мʼютекс.
bool i2c_bus_lock(uint32_t timeout_ms);
void i2c_bus_unlock(void);

// Скидає стан шини після серії невдалих транзакцій (під мʼютексом шини): ESP_OK — шина готова до нових транзакцій.
esp_err_t i2c_bus_reset(void);

// Зонд адреси (під мʼютексом шини): ESP_OK — пристрій відповів.
esp_err_t i2c_bus_probe(uint8_t addr, int timeout_ms);
