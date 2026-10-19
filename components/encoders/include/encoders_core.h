// Чиста логіка енкодерів коліс (spec AC-01…AC-10; ADR-0002): приріст за модулем межі апаратного лічильника,
// знак колеса, 64-бітний накопичувач, обнулення через зсув, швидкість як приріст на фактичний час. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENC_WHEELS 4
#define ENC_HW_LIMIT 30000                // межа апаратного лічильника: на ±30000 скидається в нуль (ADR-0002)
#define ENC_STEP_MAX (ENC_HW_LIMIT / 2)   // найбільший рух між двома вибірками, який ще розрізняється за знаком

// Порядок коліс скрізь — FL, FR, RL, RR (як ENC_DIR_SIGN у encoders_calib.h).
typedef enum { ENC_FL = 0, ENC_FR = 1, ENC_RL = 2, ENC_RR = 3 } enc_wheel_t;

typedef struct {
    bool available;      // колесо налаштоване й рахує
    char reason[40];     // причина недоступності (порожньо, якщо доступне)
    int32_t last_raw;    // попереднє сире значення апаратного лічильника
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
// запам'ятовує значення. Швидкість = приріст останнього інтервалу / фактичний час між мітками, без утримання й
// усереднення: порожній інтервал одразу дає нуль. Вибірка з тією самою міткою накопичує рух, але інтервалом не є.
// Недоступні колеса пропускаються.
void enc_core_sample(enc_state_t *s, const int32_t raw[ENC_WHEELS], uint64_t now_us);

// Обнулення показу доступних коліс: зсув = накопичувач; накопичувач не змінюється.
void enc_core_reset(enc_state_t *s);

// Показ лічильника колеса в імпульсах (накопичувач − зсув).
int64_t enc_core_count(const enc_state_t *s, enc_wheel_t wheel);
