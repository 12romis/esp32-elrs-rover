// Чиста логіка розширювача портів (T2, spec AC-14, AC-15, AC-16; ADR-0003).
#include "ioexp_core.h"

#include <string.h>

void ioexp_init(ioexp_t *e)
{
    if (e == NULL) {
        return;
    }
    memset(e, 0, sizeof(*e));
    e->status = IOEXP_STATUS_UNKNOWN;
}

void ioexp_start_result(ioexp_t *e, bool ack)
{
    if (e == NULL || e->status != IOEXP_STATUS_UNKNOWN) {
        return;  // результат старту фіксується один раз; недоступний лишається таким до перезапуску
    }
    if (ack) {
        e->status = IOEXP_STATUS_AVAILABLE;
        e->shadow = 0;
        e->bus_fault = false;
    } else {
        e->status = IOEXP_STATUS_UNAVAILABLE;
    }
}

bool ioexp_available(const ioexp_t *e)
{
    return e != NULL && e->status == IOEXP_STATUS_AVAILABLE;
}

bool ioexp_prepare(const ioexp_t *e, uint8_t desired, uint8_t *out_byte)
{
    if (!ioexp_available(e) || out_byte == NULL) {
        return false;
    }
    *out_byte = (uint8_t)((desired & IOEXP_LIGHT_MASK) | (e->xshut & IOEXP_XSHUT_MASK));
    return true;
}

bool ioexp_needs_write(const ioexp_t *e, uint8_t desired)
{
    if (!ioexp_available(e)) {
        return false;
    }
    return e->bus_fault || (uint8_t)((desired & IOEXP_LIGHT_MASK) | (e->xshut & IOEXP_XSHUT_MASK)) != e->shadow;
}

ioexp_event_t ioexp_commit(ioexp_t *e, uint8_t byte, bool ack)
{
    if (!ioexp_available(e)) {
        return IOEXP_EVT_NONE;
    }
    if (ack) {
        e->shadow = (uint8_t)(byte & (IOEXP_LIGHT_MASK | IOEXP_XSHUT_MASK));
        if (e->bus_fault) {
            e->bus_fault = false;
            return IOEXP_EVT_RECOVERED;
        }
        return IOEXP_EVT_NONE;
    }
    if (!e->bus_fault) {
        e->bus_fault = true;
        return IOEXP_EVT_FAULT;
    }
    return IOEXP_EVT_NONE;
}

bool ioexp_xshut_set(ioexp_t *e, int bit, bool high)
{
    if (!ioexp_available(e) || bit < 0 || bit > 7 || ((IOEXP_XSHUT_MASK >> bit) & 1u) == 0) {
        return false;
    }
    if (high) {
        e->xshut = (uint8_t)(e->xshut | (1u << bit));
    } else {
        e->xshut = (uint8_t)(e->xshut & ~(1u << bit));
    }
    return true;
}

void ioexp_xshut_revert(ioexp_t *e)
{
    if (e != NULL) {
        e->xshut = (uint8_t)(e->shadow & IOEXP_XSHUT_MASK);
    }
}

bool ioexp_bit(const ioexp_t *e, int bit)
{
    if (e == NULL || bit < 0 || bit > 7) {
        return false;
    }
    return (e->shadow >> bit) & 1u;
}
