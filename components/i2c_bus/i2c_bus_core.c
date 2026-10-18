// Чиста логіка шини периферії: результат скану (T1, spec AC-01, AC-02).
#include "i2c_bus_core.h"

#include <stdio.h>
#include <string.h>

void i2c_scan_init(i2c_scan_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

static bool in_range(uint8_t addr)
{
    return addr >= I2C_BUS_ADDR_MIN && addr <= I2C_BUS_ADDR_MAX;
}

bool i2c_scan_contains(const i2c_scan_t *s, uint8_t addr)
{
    if (s == NULL) {
        return false;
    }
    for (size_t i = 0; i < s->count; i++) {
        if (s->addrs[i] == addr) {
            return true;
        }
    }
    return false;
}

bool i2c_scan_add(i2c_scan_t *s, uint8_t addr)
{
    if (s == NULL || !in_range(addr) || s->count >= I2C_SCAN_CAPACITY || i2c_scan_contains(s, addr)) {
        return false;
    }
    s->addrs[s->count++] = addr;
    return true;
}

size_t i2c_scan_others(const i2c_scan_t *s, uint8_t expected)
{
    if (s == NULL) {
        return 0;
    }
    size_t others = 0;
    for (size_t i = 0; i < s->count; i++) {
        if (s->addrs[i] != expected) {
            others++;
        }
    }
    return others;
}

void i2c_scan_format(char *buf, size_t n, const i2c_scan_t *s, uint8_t expected)
{
    if (buf == NULL || n == 0) {
        return;
    }
    buf[0] = '\0';
    if (s == NULL) {
        return;
    }
    size_t pos = 0;
    int w;
    if (s->count == 0) {
        w = snprintf(buf, n, "на шині нічого не знайдено");
    } else {
        w = snprintf(buf, n, "знайдено %u:", (unsigned)s->count);
    }
    pos = (w > 0 && (size_t)w < n) ? (size_t)w : n - 1;
    for (size_t i = 0; i < s->count && pos < n - 1; i++) {
        w = snprintf(buf + pos, n - pos, " 0x%02X", (unsigned)s->addrs[i]);
        pos = (w > 0 && pos + (size_t)w < n) ? pos + (size_t)w : n - 1;
    }
    if (pos < n - 1) {
        snprintf(buf + pos, n - pos, "; очікувана 0x%02X %s", (unsigned)expected,
                 i2c_scan_contains(s, expected) ? "є" : "не знайдена");
    }
}
