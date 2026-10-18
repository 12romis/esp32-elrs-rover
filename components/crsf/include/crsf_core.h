// Чиста логіка приймання кадрів CRSF (spec AC-06, AC-10, AC-11; ADR-0001, ADR-0003): збирач кадрів із потоку байтів,
// CRC8, розпакування 16 каналів, лічильники. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Формат кадру: [0xC8][len][type][payload][crc8]; len = type + payload + crc8; CRC8 з поліномом 0xD5 по type+payload.
#define CRSF_SYNC_BYTE 0xC8
#define CRSF_TYPE_RC_CHANNELS 0x16
#define CRSF_LEN_MIN 2
#define CRSF_LEN_MAX 62
#define CRSF_LEN_RC 24            // type(1) + 22 байти каналів + crc8(1)
#define CRSF_FRAME_MAX 64         // len(≤62) + sync + len
#define CRSF_CHANNELS 16

// Робочий діапазон каналу й центр — властивості протоколу (документ 02 §7 «Фаза 3»).
#define CRSF_RAW_MIN 172
#define CRSF_RAW_MAX 1811
#define CRSF_RAW_CENTER 992

// Індекси каналів (P5, docs/measured.md): CH1 поворот, CH2 тяга, CH5 тумблер озброєння.
#define CRSF_IDX_STEER 0
#define CRSF_IDX_THROTTLE 1
#define CRSF_IDX_ARM 4

typedef struct {
    uint32_t rc_ok;       // прийняті кадри каналів
    uint32_t crc_fail;    // неправильна контрольна сума
    uint32_t len_fail;    // неправильна довжина
    uint32_t other;       // валідні кадри інших типів (наприклад, статистика 0x14)
    uint32_t overflow;    // події переповнення буфера приймача
} crsf_counters_t;

// Знімок, який приймальна задача публікує, а керування й консоль читають.
typedef struct {
    bool have_frame;                  // хоча б один кадр каналів прийнято від старту
    uint32_t seq;                     // номер останнього кадру каналів (= rc_ok)
    uint32_t t_ms;                    // мітка прийняття останнього кадру каналів
    uint16_t ch[CRSF_CHANNELS];       // канали, обмежені до CRSF_RAW_MIN…CRSF_RAW_MAX
    crsf_counters_t cnt;
} crsf_snapshot_t;

typedef struct {
    uint8_t buf[CRSF_FRAME_MAX];
    size_t n;
    crsf_snapshot_t snap;
} crsf_parser_t;

void crsf_parser_init(crsf_parser_t *p);

// CRC8 з поліномом 0xD5, початкове значення 0.
uint8_t crsf_crc8(const uint8_t *data, size_t n);

// Подає порцію байтів. Повертає, скільки кадрів каналів прийнято в цій порції.
// Мітка now_ms ставиться кадрам каналів, що завершилися в цій порції.
size_t crsf_feed(crsf_parser_t *p, const uint8_t *data, size_t n, uint32_t now_ms);

// Подія переповнення буфера приймача: вхідний буфер скидається, лічильник +1.
void crsf_note_overflow(crsf_parser_t *p);

// ---------- Намір пілота (T2; ADR-0003): канали → тяга, поворот, тумблер ----------

// Мертва зона: ±5 % ходу від центру 992, окремо для кожної осі (квадрат, не коло); за її межею вихід росте
// лінійно від нуля без стрибка (spec §6). Нормалізація: (raw − 992) / 819, обмеження до [−1, 1].
#define CRSF_DEADBAND 0.05f
#define CRSF_HALF_TRAVEL 819.0f
// Тумблер озброєння: канал 5 вище центру діапазону — ARM, інакше DISARM (spec §6).
#define CRSF_SWITCH_ARM_ABOVE CRSF_RAW_CENTER

// Вісь після мертвої зони, −1…+1. sign — множник напрямку з crsf_calib.h.
float crsf_axis(uint16_t raw, int sign);
// Чи лежить вісь у мертвій зоні (критерій нейтралі для озброєння, spec AC-04).
bool crsf_axis_neutral(uint16_t raw);
bool crsf_switch_arm(uint16_t raw);

typedef enum { CRSF_SW_UNKNOWN, CRSF_SW_DISARM, CRSF_SW_ARM } crsf_switch_t;

typedef struct {
    float throttle;          // −1…+1 після мертвої зони, зі знаком вперед = +
    float steer;             // −1…+1, праворуч = +
    bool throttle_neutral;
    bool steer_neutral;
    crsf_switch_t sw;        // ARM або DISARM за останнім кадром
} crsf_intent_t;

// Намір із знімка. Для знімка без кадрів: усе нейтрально, тумблер DISARM.
void crsf_intent(const crsf_snapshot_t *s, crsf_intent_t *out);

// Фронт тумблера. base — попереднє положення: UNKNOWN на старті й після кожної втрати зв'язку (тоді перший
// кадр лише встановлює базу). Повертає true на зміні DISARM → ARM. Оновлює базу.
bool crsf_edge(crsf_switch_t *base, crsf_switch_t now);

// ---------- Рядки status (T2; spec AC-12; contracts/cli.md) ----------

typedef enum { CRSF_SRC_BENCH, CRSF_SRC_REMOTE, CRSF_SRC_NONE } crsf_source_t;
typedef enum { CRSF_LINK_NEVER, CRSF_LINK_ALIVE, CRSF_LINK_LOST } crsf_link_t;

typedef struct {
    crsf_source_t source;      // стенд — увесь стендовий режим; пульт — звʼязок живий; немає — стенд закінчено, звʼязок втрачено
    crsf_link_t link;
    uint32_t since_last_ms;    // час від останнього кадру каналів (лише якщо link != NEVER)
    crsf_snapshot_t snap;
    crsf_intent_t intent;
    const char *arm_block;     // NULL — озброєння можливе; інакше перша невиконана умова простими словами
    const char *failsafe_cause;// NULL — не у FAILSAFE
} crsf_status_view_t;

// Заповнює buf багаторядковим текстом для status. Повертає довжину.
size_t crsf_fmt_status(char *buf, size_t n, const crsf_status_view_t *v);
