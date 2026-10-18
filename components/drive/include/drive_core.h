// Чиста логіка руху (spec AC-03, AC-04, AC-10, AC-20, AC-21): мікшер, вирівнювання, обмеження потужності,
// рівні виходів. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"
#include "battery_core.h"
#include "crsf_core.h"

// Обмеження потужності борту, проміле від повної (конституція, п. 7: ШІМ ≤ 0.75 до завершення фази 5).
#define DRIVE_PWM_MAX_PERMILLE 750

typedef struct {
    uint16_t duty_permille;  // 0…DRIVE_PWM_MAX_PERMILLE
    uint8_t dir1;            // рівень першої лінії напрямку борту
    uint8_t dir2;            // рівень другої лінії напрямку борту
} drive_side_t;

typedef struct {
    drive_side_t left;
    drive_side_t right;
    uint8_t stby;            // 1 — драйвери ввімкнені
} drive_outputs_t;

// Мікшер танкового приводу: ліве = тяга + поворот, праве = тяга − поворот. Якщо модуль більший за 1,
// обидва значення діляться на більший модуль (співвідношення зберігається).
void drive_mix(float throttle, float steer, float *left, float *right);

// Рівні виходів для значень бортів у [−1, +1]: насичення до DRIVE_PWM_MAX_PERMILLE, знак → лінії напрямку
// (вперед 1/0, назад 0/1, нуль 0/0), STBY піднята.
drive_outputs_t drive_outputs_from(float left, float right);

// Вибіг (ADR-0002): ШІМ 0, обидві лінії напрямку низькі, STBY низька.
drive_outputs_t drive_outputs_off(void);

// ---------- Крок керування (T3, T4): запити arm/disarm/drive, дозволи, вікно команди, відсічка ----------

typedef enum { DRIVE_REQ_NONE, DRIVE_REQ_ARM, DRIVE_REQ_DISARM, DRIVE_REQ_DRIVE } drive_req_kind_t;

typedef struct {
    drive_req_kind_t kind;
    float throttle;  // −1…+1
    float steer;     // −1…+1
} drive_req_t;

typedef enum {
    DRIVE_REASON_NONE,
    DRIVE_REASON_NOT_DISARMED,
    DRIVE_REASON_NOT_ARMED,
    DRIVE_REASON_LOW_VOLTAGE,
    DRIVE_REASON_NO_MEASUREMENT,
    DRIVE_REASON_OUT_OF_RANGE,
    DRIVE_REASON_TRANSITION_REFUSED,  // автомат станів відхилив запитаний перехід
    DRIVE_REASON_REMOTE_ONLY          // стендовий режим закінчився: озброєння й рух задає лише пульт (ADR-0003)
} drive_reason_t;

typedef enum {
    DRIVE_STOP_NONE,
    DRIVE_STOP_WINDOW_EXPIRED,
    DRIVE_STOP_LOW_VOLTAGE,
    DRIVE_STOP_STALE_VOLTAGE,
    DRIVE_STOP_DISARM,
    DRIVE_STOP_LINK_LOST,        // втрата звʼязку з пультом у ARMED (phase3-crsf)
    DRIVE_STOP_REMOTE_APPEARED,  // перший кадр пульта в стенді закінчив консольне керування
    DRIVE_STOP_SWITCH_DISARM     // тумблер пульта повернуто в DISARM
} drive_stop_t;

// Вікно команди руху (спливає саме), мс. Рішення Розробника 2026-10-18.
#define DRIVE_WINDOW_MS 2000

typedef struct {
    bool window_active;
    uint32_t accepted_ms;      // момент прийняття останньої команди руху
    drive_outputs_t outputs;   // виходи, що діють, поки вікно відкрите
    int16_t left_permille;     // застосовані значення бортів зі знаком
    int16_t right_permille;
    uint32_t min_mv;           // мінімальна напруга за час вікна
    // --- phase3-crsf ---
    bool remote_active;        // стендовий режим закінчено: від старту прийшов хоча б один валідний кадр каналів
    bool link_alive;           // звʼязок живий (останній кадр каналів не старший за 500 мс)
    uint32_t link_since_ms;    // мітка першого кадру після останньої втрати (початок стабільного звʼязку)
    crsf_switch_t sw_base;     // попереднє положення тумблера; UNKNOWN на старті й після кожної втрати звʼязку
} drive_ctl_t;

// Таймер звʼязку: втрата, коли минуло строго більше 500 мс від мітки останнього кадру каналів (конституція, п. 7).
#define DRIVE_LINK_TIMEOUT_MS 500

// Події кроку для журналу HAL (по одній на подію, не на такт).
#define DRIVE_EVT_REMOTE_APPEARED (1u << 0)   // перший кадр пульта: стенд закінчено
#define DRIVE_EVT_LINK_LOST (1u << 1)
#define DRIVE_EVT_LINK_RESTORED (1u << 2)
#define DRIVE_EVT_REMOTE_ARMED (1u << 3)      // озброєно тумблером пульта
#define DRIVE_EVT_ARM_REFUSED (1u << 4)       // фронт тумблера відхилено: причина в arm_block

// Що блокує озброєння з пульта (spec AC-04). Порядок перевірки: звʼязок → напруга → нейтраль стіків → фронт тумблера.
typedef enum {
    DRIVE_BLOCK_NONE,           // можна: тумблер перемкнеться з DISARM у ARM (або вже перемкнувся)
    DRIVE_BLOCK_NOT_DISARMED,   // ровер не в DISARMED
    DRIVE_BLOCK_NO_LINK,        // звʼязку немає (чи пульт ще не з'являвся)
    DRIVE_BLOCK_NO_MEASUREMENT, // вимірювання напруги немає або застаріло
    DRIVE_BLOCK_LOW_VOLTAGE,    // напруга нижча за поріг
    DRIVE_BLOCK_NOT_NEUTRAL,    // стік не в нейтралі
    DRIVE_BLOCK_NO_EDGE         // тумблер уже в ARM (чи база невідома): треба повернути в DISARM і знову в ARM
} drive_block_t;

typedef struct {
    uint32_t now_ms;
    app_state_t state;
    battery_snapshot_t battery;
    drive_req_t req;
    crsf_snapshot_t rc;          // знімок приймача (phase3-crsf); без кадрів (have_frame = false) — стендовий режим
} drive_ctl_in_t;

typedef struct {
    drive_outputs_t out;         // виходи, які HAL застосовує ПЕРЕД запитом переходу стану
    bool has_state_request;
    app_state_t state_request;   // ARMED, DISARMED або FAILSAFE
    bool accepted;               // запит прийнято (для req.kind != NONE)
    drive_reason_t reason;       // причина відмови
    uint32_t mv;                 // виміряна напруга для пояснення відмови
    drive_stop_t stop;           // причина зупинки, якщо цей крок вимкнув виходи
    int16_t left_permille;       // застосовані значення бортів зі знаком (0 після зупинки)
    int16_t right_permille;
    uint32_t remaining_ms;       // скільки лишилось діяти команді руху (0, якщо команди немає)
    uint32_t min_mv;             // мінімальна напруга за останнє вікно (для журналу, spec OQ-3)
    app_failsafe_cause_t failsafe_cause;  // причина, якщо state_request == FAILSAFE (ADR-0002)
    drive_block_t arm_block;     // перша невиконана умова, якщо events & DRIVE_EVT_ARM_REFUSED
    uint32_t events;             // DRIVE_EVT_* для журналу
} drive_ctl_out_t;

void drive_ctl_init(drive_ctl_t *c);

// Один такт керування (20 мс). Чиста функція часу й входів: рішення про виходи й запит стану.
// Інваріант: коли has_state_request, виходи вже вимкнені (ADR-0002).
void drive_ctl_step(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *out);

// Пояснення відмови українською (mv — виміряна напруга для LOW_VOLTAGE).
void drive_reason_text(drive_reason_t r, uint32_t mv, char *buf, size_t n);

// Причина зупинки українською (для рядка «остання зупинка» в status).
const char *drive_stop_text(drive_stop_t s);

// Пояснення відповіді на disarm (AC-09): що сталося зі станом. has_req/req — запит переходу, який видав control.
void drive_disarm_text(app_state_t state_before, bool has_req, app_state_t req, drive_stop_t stop, char *buf, size_t n);

// Перша невиконана умова озброєння з пульта у поточному стані (для status і журналу). NONE — озброїти можна.
drive_block_t drive_arm_block(const drive_ctl_t *c, app_state_t st, const battery_snapshot_t *b,
                              const crsf_intent_t *intent, uint32_t now_ms);

// Пояснення простими словами українською (для рядка журналу й status).
const char *drive_block_text(drive_block_t b);
