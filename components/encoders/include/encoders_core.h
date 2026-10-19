// Чиста логіка енкодерів коліс (spec AC-01…AC-10; ADR-0002): приріст за модулем межі апаратного лічильника,
// знак колеса, 64-бітний накопичувач, обнулення через зсув, швидкість як приріст на фактичний час. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENC_WHEELS 4
#define ENC_HW_LIMIT 30000                // межа апаратного лічильника: на ±30000 скидається в нуль (ADR-0002)
#define ENC_STEP_MAX (ENC_HW_LIMIT / 2)   // найбільший рух між двома вибірками, який ще розрізняється за знаком
#define ENC_PERIOD_US 20000              // номінальний період вибірки, мкс (sad.md §4)
#define ENC_MIN_INTERVAL_US (ENC_PERIOD_US / 2)   // коротша вибірка (наздоганяння після паузи задачі) інтервалом не є

// Порядок коліс скрізь — FL, FR, RL, RR (як ENC_DIR_SIGN у encoders_calib.h).
typedef enum { ENC_FL = 0, ENC_FR = 1, ENC_RL = 2, ENC_RR = 3 } enc_wheel_t;

typedef struct {
    bool available;      // колесо налаштоване й рахує
    const char *reason;  // причина недоступності, UTF-8, рядок статичного часу життя (NULL, якщо доступне)
    int32_t last_raw;    // попереднє сире значення апаратного лічильника
    int32_t pending;     // рух зі знаком колеса від мітки останнього інтервалу (вибірки, що інтервалом не стали)
    int64_t accum;       // накопичувач зі знаком колеса від старту
    int64_t offset;      // зсув обнулення: показ = accum − offset
    float speed_pps;     // швидкість за останній інтервал, імпульсів за секунду (знак: вперед = плюс)
} enc_wheel_state_t;

typedef struct {
    enc_wheel_state_t w[ENC_WHEELS];
    bool primed;         // перша вибірка вже була: є з чим порівнювати
    uint64_t last_us;    // мітка попередньої вибірки, мкс від старту
    uint32_t intervals;  // лічильник інтервалів вимірювання від старту
} enc_state_t;

// Усі колеса доступні, лічильники нульові.
void enc_core_init(enc_state_t *s);

// Приріст між двома сирими значеннями за модулем ENC_HW_LIMIT, зведений до (−ENC_STEP_MAX, ENC_STEP_MAX].
int32_t enc_core_delta(int32_t prev_raw, int32_t raw);

// Крок вибірки: сирі значення чотирьох лічильників (FL, FR, RL, RR) і мітка вибірки в мкс. Перша вибірка лише
// запам'ятовує значення. Інтервал — вибірка не раніше ніж через ENC_MIN_INTERVAL_US після мітки попереднього; швидкість =
// весь рух від тієї мітки / фактичний час, без утримання й усереднення: порожній інтервал одразу дає нуль. Коротша
// вибірка (зокрема з тією самою міткою чи наздоганяння після паузи задачі) накопичує рух, але швидкість, мітку й
// лічильник інтервалів не змінює. Недоступні колеса пропускаються.
void enc_core_sample(enc_state_t *s, const int32_t raw[ENC_WHEELS], uint64_t now_us);

// Обнулення показу доступних коліс: зсув = накопичувач; накопичувач не змінюється.
void enc_core_reset(enc_state_t *s);

// Показ лічильника колеса в імпульсах (накопичувач − зсув).
int64_t enc_core_count(const enc_state_t *s, enc_wheel_t wheel);

// Позначає колесо недоступним з причиною (назва помилки драйвера чи літерал); вибірки й обнулення його пропускають.
// Рядок не копіюється: він має жити весь час роботи (esp_err_to_name і літерали це гарантують).
void enc_core_set_unavailable(enc_state_t *s, enc_wheel_t wheel, const char *reason);

// Скільки коліс доступні (0…4).
unsigned enc_core_available_count(const enc_state_t *s);

// Перерахунок одиниць (ENC_PULSES_PER_REV, WHEEL_DIAMETER_MM з encoders_calib.h).
double enc_core_revs(int64_t count);     // оберти колеса
double enc_core_cm(int64_t count);       // пройдена відстань, см
double enc_core_rpm(float speed_pps);    // оберти колеса за хвилину
double enc_core_cm_s(float speed_pps);   // лінійна швидкість, см/с

// Тексти для консолі (contracts/cli.md). Пишуть у буфер виклику, обрізають за його розміром, завжди завершують нулем
// (якщо n > 0) і повертають довжину записаного тексту.
// Знімок енкодерів: мітка останньої вибірки й лічильник інтервалів, далі по рядку на колесо FL, FR, RL, RR.
size_t enc_core_format_snapshot(const enc_state_t *s, char *buf, size_t n);
// Відповідь enc reset (викликати після enc_core_reset): що обнулено, і перелік недоступних коліс.
size_t enc_core_format_reset(const enc_state_t *s, char *buf, size_t n);
// Рядок status: «енкодери: N з 4 доступні[, недоступні: …]», без причин.
size_t enc_core_format_status(const enc_state_t *s, char *buf, size_t n);
