// Чиста логіка шини периферії (spec AC-01, AC-02, ADR-0001): оцінка результату скану й рядок журналу.
// Без заголовків ESP-IDF і FreeRTOS. Скан лише журналить: доступність розширювача він не визначає (ADR-0003).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define I2C_BUS_ADDR_MIN 0x08  // перша адреса скану
#define I2C_BUS_ADDR_MAX 0x77  // остання адреса скану
#define I2C_SCAN_CAPACITY (I2C_BUS_ADDR_MAX - I2C_BUS_ADDR_MIN + 1)

typedef struct {
    uint8_t addrs[I2C_SCAN_CAPACITY];  // знайдені адреси в порядку надходження
    size_t count;
} i2c_scan_t;

void i2c_scan_init(i2c_scan_t *s);

// Додає знайдену адресу. false — адреса поза діапазоном скану або вже додана (стан не змінюється).
bool i2c_scan_add(i2c_scan_t *s, uint8_t addr);

bool i2c_scan_contains(const i2c_scan_t *s, uint8_t addr);

// Скільки знайдено адрес, окрім очікуваної.
size_t i2c_scan_others(const i2c_scan_t *s, uint8_t expected);

// Рядок журналу українською: скільки знайдено, перелік адрес і чи є очікувана.
void i2c_scan_format(char *buf, size_t n, const i2c_scan_t *s, uint8_t expected);
