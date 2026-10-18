// Чиста логіка вимірювання напруги. Без заголовків ESP-IDF і FreeRTOS.
#include "battery_core.h"

#include "battery_calib.h"

uint32_t battery_avg_mv(const uint16_t *samples, size_t n)
{
    if (samples == NULL || n == 0) {
        return 0;
    }
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        sum += samples[i];
    }
    return sum / (uint32_t)n;
}

uint32_t battery_adc_to_mv(uint32_t adc_mv)
{
    return adc_mv * BATTERY_SCALE_PPM / 1000u + BATTERY_OFFSET_MV;
}

bool battery_is_below_cutoff(uint32_t mv)
{
    return mv < BATTERY_CUTOFF_MV;
}

bool battery_snapshot_usable(const battery_snapshot_t *snap, uint32_t now_ms)
{
    if (snap == NULL || !snap->valid) {
        return false;
    }
    // Знаковий вік: знімок, записаний іншим ядром після зчитування now (t_ms > now), вважається свіжим.
    return (int32_t)(now_ms - snap->t_ms) <= (int32_t)BATTERY_STALE_MS;
}
