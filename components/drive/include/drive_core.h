// Чиста логіка руху (spec AC-03, AC-04, AC-10, AC-20, AC-21): мікшер, вирівнювання, обмеження потужності,
// рівні виходів. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"
#include "battery_core.h"
#include "crsf_core.h"
#include "encoders_core.h"
#include "pid_core.h"
#include "tof_core.h"

// Межа потужності борту, проміле від повної (phase5-pid spec AC-09: 0.75 у фазі 5; підняття винесено разом із захистом
// від заклинення, spec §3). Консоль її не змінює.
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
    DRIVE_REASON_REMOTE_ONLY,         // стендовий режим закінчився: озброєння й рух задає лише пульт (ADR-0003)
    DRIVE_REASON_PID_SAVING,          // триває збереження набору регулятора в памʼять плати (phase5-pid AC-15)
    DRIVE_REASON_PID_MODE             // триває зміна режиму регулятора з консолі (phase5-pid AC-15, рев'ю R1)
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
    // --- phase5-pid (ADR-0001) ---
    float win_mix_l, win_mix_r;    // вихід мікшера стендової команди на вікно (виходи перераховуються щотакту)
    bool armed_prev;               // попередній такт був у ARMED: вхід в ARMED фіксує режим регулятора
    pid_mode_t pid_mode;           // режим регулятора, зафіксований при вході в ARMED (spec AC-15, AC-16)
    pid_side_t pid_side[2];        // стан регулятора лівого й правого борту
    float ramp_rpm[2];             // задана після розгону, об/хв
    uint32_t last_ms;              // мітка попереднього такту (dt розгону)
    bool enc_seen;                 // знімок енкодерів уже оброблено хоч раз
    uint32_t enc_intervals;        // лічильник інтервалів останнього обробленого знімка
    uint64_t enc_t_us;             // мітка останнього обробленого знімка
    bool enc_stale;                // знімок застарів (spec AC-12b): подія журналу — один раз
    // --- phase6-tof (ADR-0001) ---
    bool tof_stale;                // знімок далекомірів застарів чи не отримано: подія журналу — один раз
    bool occ_front, occ_rear;      // сектори зайняті в цьому такті (знімок свіжий і сектор зайнятий)
    bool clamp_reported;           // про обрізання тяги вже пояснено, доки команди лишаються обрізаними (spec AC-15: один раз)
    pid_trace_acc_t trace;         // накопичення кадру трасування (ADR-0003)
} drive_ctl_t;

// Таймер звʼязку: втрата, коли минуло строго більше 500 мс від мітки останнього кадру каналів (конституція, п. 7).
#define DRIVE_LINK_TIMEOUT_MS 500

// Події кроку для журналу HAL (по одній на подію, не на такт).
#define DRIVE_EVT_REMOTE_APPEARED (1u << 0)   // перший кадр пульта: стенд закінчено
#define DRIVE_EVT_LINK_LOST (1u << 1)
#define DRIVE_EVT_LINK_RESTORED (1u << 2)
#define DRIVE_EVT_REMOTE_ARMED (1u << 3)      // озброєно тумблером пульта
#define DRIVE_EVT_ARM_REFUSED (1u << 4)       // фронт тумблера відхилено: причина в arm_block
#define DRIVE_EVT_FAILSAFE_EXIT (1u << 5)     // запит виходу FAILSAFE → DISARMED (звʼязок відновлено)
#define DRIVE_EVT_CAUSE_UPGRADE (1u << 6)     // відсічка по напрузі у FAILSAFE через звʼязок: причина стає «напруга»
#define DRIVE_EVT_ENC_STALE (1u << 7)         // знімок енкодерів застарів: обидва борти без регулятора (phase5-pid AC-12b)
#define DRIVE_EVT_ENC_RESTORED (1u << 8)      // знімок енкодерів знову оновлюється: регулятор з нуля
#define DRIVE_EVT_HOLD_ENTER (1u << 9)        // запит OBSTACLE_HOLD: сектор зайнятий (hold_sector, hold_mm, hold_nearest)
#define DRIVE_EVT_HOLD_EXIT (1u << 10)        // запит ARMED з OBSTACLE_HOLD: сектори вільні
#define DRIVE_EVT_TOF_STALE (1u << 11)        // знімок далекомірів застарів чи не отримано: сектори не охороняються
#define DRIVE_EVT_TOF_RESTORED (1u << 12)     // знімок далекомірів знову оновлюється

// Стабільний звʼязок для виходу з FAILSAFE: безперервно живий від першого кадру після втрати (spec AC-08).
#define DRIVE_LINK_STABLE_MS 1000

// Що блокує озброєння з пульта (spec AC-04). Порядок перевірки: звʼязок → напруга → нейтраль стіків → фронт тумблера.
typedef enum {
    DRIVE_BLOCK_NONE,           // можна: тумблер перемкнеться з DISARM у ARM (або вже перемкнувся)
    DRIVE_BLOCK_NOT_DISARMED,   // ровер не в DISARMED
    DRIVE_BLOCK_NO_LINK,        // звʼязку немає (чи пульт ще не з'являвся)
    DRIVE_BLOCK_NO_MEASUREMENT, // вимірювання напруги немає або застаріло
    DRIVE_BLOCK_LOW_VOLTAGE,    // напруга нижча за поріг
    DRIVE_BLOCK_NOT_NEUTRAL,    // стік не в нейтралі
    DRIVE_BLOCK_NO_EDGE,        // тумблер уже в ARM (чи база невідома): треба повернути в DISARM і знову в ARM
    DRIVE_BLOCK_PID_SAVING,     // триває збереження набору регулятора в памʼять плати (phase5-pid AC-15)
    DRIVE_BLOCK_PID_MODE        // триває зміна режиму регулятора з консолі (phase5-pid AC-15, рев'ю R1)
} drive_block_t;

// Знімок енкодерів для регулятора (phase5-pid; ADR-0001): швидкість колеса в об/хв, порядок FL, FR, RL, RR.
typedef struct {
    bool valid;                  // знімок отримано (задача encoders працює)
    bool avail[ENC_WHEELS];
    float rpm[ENC_WHEELS];
    uint32_t intervals;          // лічильник інтервалів вимірювання
    uint64_t t_us;               // мітка останнього інтервалу, мкс від старту (та сама шкала, що й now_ms × 1000)
} drive_enc_in_t;

// Знімок вважається застарілим, якщо від мітки інтервалу минуло понад стільки мс (spec AC-12b; рішення Розробника).
#define DRIVE_ENC_STALE_MS 100

typedef struct {
    uint32_t now_ms;
    app_state_t state;
    battery_snapshot_t battery;
    drive_req_t req;
    crsf_snapshot_t rc;          // знімок приймача (phase3-crsf); без кадрів (have_frame = false) — стендовий режим
    app_failsafe_cause_t failsafe_cause;  // причина поточного FAILSAFE з app_state (NONE поза FAILSAFE)
    bool outputs_inhibited;      // control не підписано на Task WDT (ADR-0004 фази 1): виходи вимкнені, ARMED не запитується
    // --- phase5-pid ---
    drive_enc_in_t enc;          // знімок енкодерів; нульова ініціалізація — енкодерів немає (режим без регулятора)
    pid_set_t pid;               // копія набору коефіцієнтів і режиму; нульова ініціалізація — «не налаштований»
    bool pid_saving;             // триває запис набору в памʼять плати: озброєння відхиляється (spec AC-15)
    // --- phase6-tof ---
    tof_view_t tof;              // знімок далекомірів; нульова ініціалізація (valid == false) — захисту немає, сектори вільні
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
    // --- phase5-pid ---
    pid_side_view_t pid_view[2]; // стан бортів для показу `pid` і кадру трасування
    bool tof_clamped;            // у такті прийнятої команди руху тягу в бік зайнятого сектору обрізано (spec AC-15)
    uint8_t hold_sector;         // для DRIVE_EVT_HOLD_ENTER: tof_sector_t зайнятого сектору
    uint16_t hold_mm;            // його відстань
    bool hold_has_first;         // відома мітка першого відліку менше порога
    uint32_t hold_first_ms;      // мітка першого відліку менше порога (початок відліку реакції, spec AC-01)
    int8_t hold_nearest;         // датчик з найменшою відстанню (tof_id_t) або −1
    bool trace_ready;            // накопичено кадр трасування (кожні PID_TRACE_TICKS тактів у ARMED)
    pid_trace_frame_t trace;
} drive_ctl_out_t;

// Режим бортів для журналу старту (spec AC-12; рев'ю F8): за двома чи одним колесом, без регулятора (режим набору
// не «увімкнений») або без регулятора через відсутність доступного енкодера; avail — у порядку FL, FR, RL, RR.
// Повертає довжину, завжди завершує нулем (n > 0).
size_t drive_pid_boards_text(pid_mode_t mode, const bool avail[ENC_WHEELS], char *buf, size_t n);

void drive_ctl_init(drive_ctl_t *c);

// Один такт керування (20 мс). Чиста функція часу й входів: рішення про виходи й запит стану.
// Інваріант: коли has_state_request, виходи вже вимкнені (ADR-0002).
void drive_ctl_step(drive_ctl_t *c, const drive_ctl_in_t *in, drive_ctl_out_t *out);

// Дозвіл на ARMED не отримано (phase5-pid AC-15; рев'ю F1, R1, S1): HAL викликає після pid_hal_arm_begin == false.
// Знімає запит переходу; запит консолі (have_req) стає відмовою, подія пульта «озброєно» — «відмова»; причина —
// збереження набору (PID_BUSY_SAVE) чи зміна режиму (PID_BUSY_MODE).
void drive_arm_refused(drive_ctl_out_t *o, bool have_req, pid_busy_t busy);

// Пояснення відмови українською (mv — виміряна напруга для LOW_VOLTAGE).
void drive_reason_text(drive_reason_t r, uint32_t mv, char *buf, size_t n);

// Причина зупинки українською (для рядка «остання зупинка» в status).
const char *drive_stop_text(drive_stop_t s);

// Пояснення відповіді на disarm (AC-09): що сталося зі станом. has_req/req — запит переходу, який видав control.


// Перша невиконана умова озброєння з пульта у поточному стані (для status і журналу). NONE — озброїти можна.
drive_block_t drive_arm_block(const drive_ctl_t *c, app_state_t st, const battery_snapshot_t *b,
                              const crsf_intent_t *intent, uint32_t now_ms);

// Пояснення простими словами українською (для рядка журналу й status).
const char *drive_block_text(drive_block_t b);

// Затримка «кадр прийнято приймальною задачею → команда записана бортам» (spec §6, NFR ≤ 50 мс; review F5).
typedef struct {
    uint32_t n;        // кадрів у вибірці
    uint32_t min_ms;
    uint32_t max_ms;
    uint64_t sum_ms;
} drive_lat_t;

void drive_lat_reset(drive_lat_t *l);
// Беззнакова різниця applied_ms − frame_ms (переповнення мс не впливає).
void drive_lat_add(drive_lat_t *l, uint32_t frame_ms, uint32_t applied_ms);
// Рядок журналу: «затримка кадр → борти: N кадрів, мін X, сер Y, макс Z мс».
void drive_lat_fmt(const drive_lat_t *l, char *buf, size_t n);

// Тумблер для status (contracts/cli.md): база фронту кроку керування; UNKNOWN у стенді й без звʼязку (spec AC-12).
crsf_switch_t drive_switch_shown(const drive_ctl_t *c);

// Чого бракує для виходу з FAILSAFE (для status, відповіді disarm і журналу; spec AC-08, AC-09).
typedef enum {
    DRIVE_FSX_READY,          // умови виконано: вихід у DISARMED відбудеться на цьому такті
    DRIVE_FSX_RESET_ONLY,     // причина не «втрата звʼязку»: вихід лише апаратним скиданням
    DRIVE_FSX_NO_LINK,        // звʼязку немає
    DRIVE_FSX_STABILIZING,    // звʼязок живий менше DRIVE_LINK_STABLE_MS
    DRIVE_FSX_SWITCH_ARM      // тумблер у ARM: опустіть його в DISARM
} drive_fsx_t;

drive_fsx_t drive_failsafe_exit_state(const drive_ctl_t *c, app_failsafe_cause_t cause, const crsf_intent_t *intent,
                                      uint32_t now_ms);
const char *drive_fsx_text(drive_fsx_t x);

// У FAILSAFE пояснення залежить від причини й від того, чого бракує для виходу (fsx); в ARMED з тумблером пульта в ARM
// підказує, що для нового озброєння його треба повернути в DISARM (spec AC-08, AC-09, AC-16).
void drive_disarm_text(app_state_t state_before, bool has_req, app_state_t req, drive_stop_t stop,
                       app_failsafe_cause_t cause, drive_fsx_t fsx, bool switch_arm, char *buf, size_t n);
