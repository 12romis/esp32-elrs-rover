// Чиста логіка сесії (phase8-session-report, ADR-0001; spec A5–A10): межі сесії за станом автомата, статистика кроку
// керування (напруга, утримання перед перешкодою, режим контролю, пробіг, швидкості бортів) і підсумок сесії.
// Без заголовків ESP-IDF і FreeRTOS: перевіряється на хості. Викликається з кроку керування (20 мс) без блокувань.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"

#define SESSION_SENSORS 4   // лічильники утримань за датчиками, порядок tof_id_t: ліво-вперед, прямо, право-вперед, ззаду

typedef enum {
    SESSION_END_DISARM,      // роззброєння (тумблером, консоллю чи зі стану, що вимагає роззброєння)
    SESSION_END_LINK_LOST,   // FAILSAFE через втрату зв'язку
    SESSION_END_VOLTAGE      // FAILSAFE через напругу
} session_end_t;

// Підсумок завершеної сесії: джерело полів документа звіту (contracts/events.md). Значення «немає даних» мають прапорець *_valid.
typedef struct {
    uint32_t seq;
    uint32_t duration_ms;
    session_end_t end_reason;
    bool voltage_start_valid, voltage_end_valid, voltage_min_valid;
    uint32_t voltage_start_mv, voltage_end_mv, voltage_min_mv;
    // Утримання перед перешкодою
    uint32_t hold_count;
    uint32_t hold_total_ms;
    bool hold_min_valid;             // було хоч одне утримання
    uint16_t hold_min_mm;            // найменша відстань на вході в утримання
    int8_t hold_min_sensor;          // датчик цієї відстані (tof_id_t) або −1
    uint32_t hold_by_sensor[SESSION_SENSORS];   // у скількох утриманнях датчик був нижче порога на вході
    uint32_t tof_control_off_ms;     // скільки мс режим контролю був вимкнений
    // Рух
    bool left_enc_seen, right_enc_seen;   // у борту хоч раз за сесію був доступний енкодер (пояснює пробіг за одним бортом і null)
    bool distance_valid;             // є хоч один доступний енкодер
    float distance_cm;
    uint32_t moving_ms;
    bool left_speed_valid, right_speed_valid;
    float left_avg_rpm, left_peak_rpm, right_avg_rpm, right_peak_rpm;
    bool imbalance_valid;
    float imbalance_pct;             // (лівий − правий) / півсума, %; плюс — лівий швидший
} session_summary_t;

// Вхід одного кроку керування.
typedef struct {
    uint32_t now_ms;
    app_state_t state;               // стан автомата, який бачить крок
    app_failsafe_cause_t failsafe_cause;
    uint32_t next_seq;               // номер, який дістане сесія, що починається на цьому кроці (лічильник у памʼяті плати)
    bool battery_valid;              // знімок напруги придатний
    uint32_t battery_mv;
    bool tof_control_on;             // режим контролю перешкод ввімкнений
    // Подія входу в утримання (drive: DRIVE_EVT_HOLD_ENTER)
    bool hold_enter;
    uint16_t hold_mm;
    int8_t hold_nearest;             // tof_id_t або −1
    uint8_t hold_below_mask;         // біти 1u << tof_id_t датчиків нижче порога на вході
    // Рух
    int16_t left_permille, right_permille;     // застосована потужність бортів зі знаком
    bool left_avail, right_avail;              // у борту є доступний енкодер
    float left_rpm, right_rpm;                 // швидкість борту (знак: вперед = плюс), об/хв
    float left_dist_cm, right_dist_cm;         // зміщення борту від попереднього кроку, см (знак: вперед = плюс)
    float cmd_left, cmd_right;                 // задані значення бортів (вихід мікшера після обмежень): пряма їзда — рівні й ненульові
} session_in_t;

typedef struct {
    bool active;
    uint32_t seq;
    uint32_t start_ms;
    uint32_t last_ms;
    bool have_last_ms;
    app_state_t prev_state;          // стан і режим контролю попереднього кроку: інтервал до цього кроку належить їм
    bool prev_ctl_on;
    bool any_avail;                  // хоч раз за сесію був доступний енкодер
    bool last_mv_valid;
    uint32_t last_mv;
    session_summary_t sum;
    // накопичувачі швидкості: сума |rpm| × dt, мс рахується окремо по бортах (борт може бути недоступний частину часу)
    double left_rpm_ms, right_rpm_ms;
    uint32_t left_ms, right_ms;
    double str_left_rpm_ms, str_right_rpm_ms;   // лише на інтервалах прямої їзди
    uint32_t str_ms;
    double dist_cm;
} session_t;

void session_init(session_t *s);

// Один крок керування. Повертає true, коли на цьому кроці сесія завершилась: *out — її підсумок.
// Початок — перша поява руху (ARMED/OBSTACLE_HOLD) після нерухомого стану; кінець — перший крок поза рухом.
bool session_step(session_t *s, const session_in_t *in, session_summary_t *out);

// Чи триває сесія (для status).
bool session_active(const session_t *s);
