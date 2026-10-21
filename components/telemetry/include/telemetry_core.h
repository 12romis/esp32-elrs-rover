// Чиста логіка telemetry (phase7-telemetry, ADR-0001, ADR-0003; spec AC-01, AC-02, AC-04, AC-05): кадр датчика батареї для пульта,
// рішення «слати чи ні», детектор зміни збою передачі, статистика часу кроку керування. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "battery_core.h"

#define TELEMETRY_TYPE_BATTERY 0x08           // тип кадру CRSF «датчик батареї»
#define TELEMETRY_BATTERY_FRAME_LEN 12        // синхробайт, довжина, тип, 8 байтів даних, CRC8
#define TELEMETRY_PERIOD_MS 200               // період передачі (документ 02 §6.1, таблиця задач)

// Кадр датчика батареї: напруга в десятках мВ·100 (розв'язність 0.1 В, до найближчого), старший байт першим; струм, ємність і
// відсоток заряду — нулі («немає даних»: датчиків і кривої розряду немає, конституція п. 3). Повертає довжину або 0.
size_t telemetry_core_build_battery_frame(uint32_t mv, uint8_t *buf, size_t n);

// Слати можна лише придатний знімок напруги (валідний і не застарілий).
bool telemetry_core_should_send(const battery_snapshot_t *snap, uint32_t now_ms);

// Детектор зміни стану передачі: подія лише на зміну (збій, відновлення), а не на кожен період.
typedef enum { TEL_EVT_NONE, TEL_EVT_FAILED, TEL_EVT_RESTORED } tel_event_t;
typedef struct {
    bool known;
    bool ok;
} tel_link_t;
void telemetry_core_link_init(tel_link_t *l);
tel_event_t telemetry_core_link_report(tel_link_t *l, bool ok);

// Рішення «слати чи ні» з детектором зміни «знімок придатний ↔ ні» (AC-02: одне попередження на зміну, не на період).
bool telemetry_core_decide(tel_link_t *snap_link, const battery_snapshot_t *snap, uint32_t now_ms, tel_event_t *ev);

// Статистика часу кроку керування від старту плати.
typedef struct {
    uint32_t count;
    uint32_t last_us;
    uint32_t max_us;
    uint64_t sum_us;
    uint32_t over;    // кроків довших за період
} tel_step_stats_t;
void telemetry_core_step_init(tel_step_stats_t *s);
void telemetry_core_step_record(tel_step_stats_t *s, uint32_t us, uint32_t period_us);
// Рядок для status; до першого виміру — «вимірів ще немає». Повертає довжину (обрізається до n-1).
size_t telemetry_core_step_text(const tel_step_stats_t *s, uint32_t period_us, char *buf, size_t n);
