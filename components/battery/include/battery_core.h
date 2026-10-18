// Чиста логіка вимірювання напруги (spec AC-13, AC-14, AC-22). Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t mv;     // усереднена напруга акумулятора, мВ
    uint32_t t_ms;   // час вимірювання, мс
    bool valid;      // було хоча б одне вимірювання
} battery_snapshot_t;

// Середнє пакета відліків АЦП (мВ на вході АЦП); ціле ділення вниз. Порожній пакет або NULL → 0.
uint32_t battery_avg_mv(const uint16_t *samples, size_t n);

// Напруга на вході АЦП → напруга акумулятора, мВ (коефіцієнт і зсув з battery_calib.h).
uint32_t battery_adc_to_mv(uint32_t adc_mv);

// Нижче порога відсічки (рівно поріг — ще не нижче).
bool battery_is_below_cutoff(uint32_t mv);

// Знімок придатний: валідний і не старший за BATTERY_STALE_MS (вік рахується зі знаком: знімок «з майбутнього» свіжий).
bool battery_snapshot_usable(const battery_snapshot_t *snap, uint32_t now_ms);
