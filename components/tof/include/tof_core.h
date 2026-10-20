// Чиста логіка далекомірів (phase6-tof, ADR-0001, ADR-0003): медіана, сектори, зайнятість, відмова, обмеження тяги.
// Без заголовків ESP-IDF і FreeRTOS: перевіряється на хості. HAL (tof_hal.c) лише подає відліки й публікує знімок.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Параметри логіки (не виміри): див. sad.md §4. Поріг перешкоди — tof_calib.h.
#define TOF_PERIOD_MS 50          // період опитування кожного далекоміра
#define TOF_MEDIAN_N 3            // медіана з трьох останніх відліків
#define TOF_RELEASE_COUNT 3       // сектор вільний після трьох поспіль оновлень, не менших за поріг
#define TOF_FAIL_MS 250           // відмова: стільки мс без валідного відліку
#define TOF_STALE_MS 250          // знімок старший за стільки мс вважається застарілим (рахує control)
#define TOF_OUT_OF_RANGE_MM 8190  // відлік не менший за це — «поза діапазоном»: перешкоди немає

typedef enum {
    TOF_LEFT_FRONT,   // U7: вперед ліворуч під 45°
    TOF_FRONT,        // U8: прямо вперед
    TOF_RIGHT_FRONT,  // U9: вперед праворуч під 45°
    TOF_REAR,         // U10: прямо назад
    TOF_SENSORS
} tof_id_t;

typedef enum { TOF_SECTOR_FRONT, TOF_SECTOR_REAR, TOF_SECTORS } tof_sector_t;

typedef enum {
    TOF_S_INIT,       // HAL ще не запустив датчик: відмовою не вважається
    TOF_S_NOT_READY,  // запущений, але відстані ще немає (менше трьох відліків)
    TOF_S_OK,         // працює, є відстань
    TOF_S_OUT,        // працює, поза діапазоном (перешкоди немає)
    TOF_S_FAILED      // відмова (не відповів при старті або 250 мс без валідного відліку)
} tof_state_t;

typedef struct {
    tof_state_t state;
    uint16_t raw[TOF_MEDIAN_N];
    uint8_t count;           // скільки відліків у кільці (до TOF_MEDIAN_N)
    uint8_t head;
    uint32_t last_valid_ms;  // мітка останнього валідного відліку (чи запуску датчика)
    uint16_t mm;             // медіана, якщо state == OK
    bool dirty;              // з'явився відлік після попереднього tof_core_tick
} tof_sensor_t;

typedef struct {
    bool occupied;
    uint8_t free_count;      // скільки оновлень поспіль сектор не був зайнятий
} tof_sector_state_t;

typedef struct {
    tof_sensor_t s[TOF_SENSORS];
    tof_sector_state_t sec[TOF_SECTORS];
} tof_core_t;

// Події одного tof_core_tick: бітові маски за tof_id_t (1u << id).
typedef struct {
    uint8_t failed_now;     // датчики, що перейшли у відмову на цьому кроці
    uint8_t restored_now;   // датчики, що повернулися з відмови (tof_core_start)
} tof_events_t;

// Знімок для control (під portMUX у HAL); valid/seq/t_ms виставляє HAL.
typedef struct {
    tof_state_t state;
    uint16_t mm;            // лише для OK
} tof_view_sensor_t;

typedef struct {
    bool occupied;
    bool guarded;           // є хоч один працюючий датчик сектору
    uint16_t mm;            // відстань сектору (мінімум); TOF_OUT_OF_RANGE_MM, якщо всі поза діапазоном
    int8_t nearest;         // датчик з мінімальною відстанню (tof_id_t), -1 якщо немає
} tof_view_sector_t;

typedef struct {
    bool valid;
    uint32_t seq;
    uint32_t t_ms;
    tof_view_sensor_t sensor[TOF_SENSORS];
    tof_view_sector_t sector[TOF_SECTORS];
    bool protection;        // хоч один сектор охороняється
} tof_view_t;

void tof_core_init(tof_core_t *c);

// HAL: датчик успішно ініціалізований (now_ms — мітка запуску; лічильник відмови пішов). З відмови — подія restored.
void tof_core_start(tof_core_t *c, tof_id_t id, uint32_t now_ms, tof_events_t *ev);

// HAL: датчик не ініціалізований або безнадійно мовчить (подія failed_now, якщо він ще не був у відмові).
void tof_core_mark_failed(tof_core_t *c, tof_id_t id, tof_events_t *ev);

// Валідний відлік, мм (значення ≥ TOF_OUT_OF_RANGE_MM — поза діапазоном). Ігнорується для INIT і FAILED.
void tof_core_sample(tof_core_t *c, tof_id_t id, uint16_t mm, uint32_t now_ms);

// Один крок опитування: відмова за TOF_FAIL_MS, відстані й зайнятість секторів. Викликається раз на період опитування.
void tof_core_tick(tof_core_t *c, uint32_t now_ms, tof_events_t *ev);

// Після паузи опитування (tof reinit): годинники відмови справних датчиків ідуть заново.
void tof_core_resume(tof_core_t *c, uint32_t now_ms);

void tof_core_view(const tof_core_t *c, tof_view_t *v);

// Обмеження руху (ADR-0001, spec «Поведінка захисту»): ліве/праве → тяга = (л + п)/2 і поворот = (л − п)/2; тяга вперед
// обрізається знизу нулем, коли передній сектор зайнятий, назад — зверху нулем, коли зайнятий задній; поворот без змін.
// Найбільший модуль борту не зростає. Якщо жоден сектор не зайнятий, значення повертаються точно без змін.
void tof_limit_sides(float left, float right, bool front_occupied, bool rear_occupied, float *left_out, float *right_out);

// Ім'я датчика на платі: «U7», «U8», «U9», «U10».
const char *tof_sensor_name(tof_id_t id);

// Показ для консолі `tof`: датчики (місце, стан, відстань), сектори (відстань, зайнятий/вільний/не охороняється), поріг,
// режим захисту. valid == false → «ініціалізація»; stale → «знімок застарів». Повертає довжину, завжди завершує нулем.
size_t tof_fmt_show(char *buf, size_t n, const tof_view_t *v, bool stale);

// Рядок входу в утримання: сектор, відстань, датчик, поріг.
size_t tof_fmt_hold_line(char *buf, size_t n, tof_sector_t sector, uint16_t mm, int nearest);

// Датчик належить сектору: передній — три передні, задній — U10.
tof_sector_t tof_sector_of(tof_id_t id);
