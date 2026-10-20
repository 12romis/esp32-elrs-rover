// Чиста логіка регулятора швидкості бортів (phase5-pid; ADR-0001, ADR-0002): набір коефіцієнтів, перевірка чисел,
// запис набору для памʼяті плати з версією і CRC32. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------- Набір коефіцієнтів (spec «Поведінка регулятора», одиниці набору) ----------
// Похибка — в обертах колеса за хвилину, вихід — частка повної потужності. Kp — частка на 1 об/хв похибки;
// Ki — частка на 1 об/хв похибки за секунду; Kd — частка на 1 об/хв за секунду зміни швидкості;
// темп розгону — об/хв за секунду. Числа не залежать від тривалості такту.
typedef enum { PID_PARAM_KP = 0, PID_PARAM_KI, PID_PARAM_KD, PID_PARAM_RAMP, PID_PARAM_COUNT } pid_param_t;

// Режим регулятора: не налаштований (немає повного набору), вимкнений, увімкнений.
typedef enum { PID_MODE_UNCONFIGURED = 0, PID_MODE_OFF, PID_MODE_ON } pid_mode_t;

typedef struct {
    float v[PID_PARAM_COUNT];     // значення чисел набору
    bool set[PID_PARAM_COUNT];    // число задане (з консолі чи із запису)
    pid_mode_t mode;
    bool saved;                   // набір і режим збігаються з записом у памʼяті плати
} pid_set_t;

// Порожній набір: жодне число не задане, режим «не налаштований», не збережено.
void pid_set_init(pid_set_t *s);

// Усі чотири числа задані.
bool pid_set_complete(const pid_set_t *s);

// Назва числа для консолі й журналу: kp, ki, kd, ramp.
const char *pid_param_name(pid_param_t p);

// Розбір назви числа (без нуль-термінатора): kp, ki, kd, ramp. false — невідома назва.
bool pid_param_parse(const char *name, size_t len, pid_param_t *out);

// Перевірка значення (spec AC-04): kp, ki, kd — скінченні й ≥ 0; ramp — скінченний і > 0.
// NULL — значення допустиме, інакше — причина відмови українською (рядок статичного часу життя).
const char *pid_value_check(pid_param_t p, float value);

// Зміна одного числа: перевіряє значення, записує, знімає «збережено». Коли набір стає повним уперше,
// режим переходить із «не налаштований» у «вимкнений». Повертає NULL або причину відмови (набір без змін).
const char *pid_set_param(pid_set_t *s, pid_param_t p, float value);

// Увімкнення чи вимкнення регулятора. Неповний набір — відмова з причиною; режим без змін.
// Зміна режиму знімає «збережено».
const char *pid_set_mode(pid_set_t *s, bool on);

// Зміна режиму з консолі (spec AC-15): disarmed — стан DISARMED, прочитаний під ознакою зміни режиму (pid_guard_try_mode).
// Поза DISARMED повертає причину й нічого не змінює; інакше — як pid_set_mode.
const char *pid_mode_change(pid_set_t *s, bool on, bool disarmed);

// Перелік незаданих чисел через кому («kp, ramp») у буфер; повертає довжину; порожній рядок, якщо набір повний.
size_t pid_set_missing(const pid_set_t *s, char *buf, size_t n);

// ---------- Запис набору в памʼяті плати (ADR-0002) ----------
// Один blob: магічне число (u32), версія (u8), режим (u8), резерв (u16 = 0), kp, ki, kd, ramp (float32),
// CRC32 по всіх попередніх байтах. Порядок байтів — little-endian.
#define PID_RECORD_MAGIC 0x50494431u   // «PID1»
#define PID_RECORD_VERSION 1u
#define PID_RECORD_SIZE 28u

typedef enum {
    PID_REC_OK = 0,
    PID_REC_ABSENT,    // запису немає
    PID_REC_SIZE,      // розмір не той: неповний чи чужий запис
    PID_REC_MAGIC,     // чуже магічне число
    PID_REC_VERSION,   // інша версія формату
    PID_REC_CRC,       // контрольна сума не збігається: запис пошкоджено
    PID_REC_VALUE,     // число чи режим поза допустимим
    PID_REC_READ       // запис не прочитано: збій сховища (рев'ю F9)
} pid_rec_result_t;

// CRC-32 (IEEE 802.3, відбитий поліном 0xEDB88320, початок і кінець 0xFFFFFFFF).
uint32_t pid_crc32(const uint8_t *data, size_t len);

// Кодує повний набір і режим (увімкнений чи вимкнений). Неповний набір — 0 байтів, інакше PID_RECORD_SIZE.
size_t pid_record_encode(const pid_set_t *s, uint8_t out[PID_RECORD_SIZE]);

// Перевіряє й розбирає запис. PID_REC_OK — out містить чотири числа, режим і «збережено»; будь-який інший
// результат — out не змінюється: запис відкидається повністю, жодне число з нього не береться (spec AC-07).
// data == NULL або len == 0 — PID_REC_ABSENT.
pid_rec_result_t pid_record_decode(const uint8_t *data, size_t len, pid_set_t *out);

// Причина для журналу старту: «запис відсутній», «запис пошкоджено» тощо.
const char *pid_record_result_text(pid_rec_result_t r);

// ---------- Взаємне виключення озброєння, запису набору і зміни режиму (spec AC-15; рев'ю F1, N4, R1) ----------
// Запис у флеш зупиняє кеш, отже й такт control: під час руху він заборонений. Зміна режиму перевіряє стан DISARMED
// під своєю ознакою, тож озброєння не вклиниться між перевіркою і записом режиму. Перевірка «іншого немає» і позначка
// «почалося» — одна дія; HAL виконує її під своїм portMUX, тож між перевіркою й позначкою нічого не вклиниться.
typedef struct {
    bool saving;     // триває запис набору в памʼять плати
    bool changing;   // триває зміна режиму регулятора з консолі (pid on / pid off)
    bool arming;     // триває вхід в ARMED (від дозволу до завершення переходу автомата)
} pid_guard_t;

// Що заважає озброєнню: від цього залежить причина відмови.
typedef enum { PID_BUSY_NONE = 0, PID_BUSY_SAVE, PID_BUSY_MODE } pid_busy_t;

void pid_guard_init(pid_guard_t *g);
bool pid_guard_try_save(pid_guard_t *g);   // false — триває озброєння, інший запис чи зміна режиму
void pid_guard_end_save(pid_guard_t *g);
bool pid_guard_try_mode(pid_guard_t *g);   // false — триває озброєння, запис чи інша зміна режиму
void pid_guard_end_mode(pid_guard_t *g);
// false — триває запис набору (why = PID_BUSY_SAVE) чи зміна режиму (PID_BUSY_MODE); true — why = PID_BUSY_NONE.
bool pid_guard_try_arm(pid_guard_t *g, pid_busy_t *why);
void pid_guard_end_arm(pid_guard_t *g);

// ---------- Крок регулятора (spec «Поведінка регулятора»; sad.md §4) ----------
// Стеля заданої швидкості: повний стік = 150 обертів колеса за хвилину (рішення Розробника, 2026-10-19, spec §1).
#define PID_TARGET_MAX_RPM 150.0f
// Межі dt кроку регулятора, с (sad.md §4): захист від нульового й завеликого інтервалу між знімками.
#define PID_DT_MIN_S 0.010f
#define PID_DT_MAX_S 0.040f

// Стан регулятора одного борту. Скидається при нульовій цілі, зміні напрямку і кожній зупинці.
typedef struct {
    float integ;       // накопичена складова, частка потужності в напрямку цілі (0…межа)
    float prev_speed;  // швидкість борту попереднього кроку, об/хв (для D від виміру)
    bool has_prev;     // попередній відлік є; після скидання D першого кроку = 0
    int8_t dir;        // напрямок цілі останнього кроку: +1, −1 або 0
    float out;         // останній вихід зі знаком напрямку, частка потужності
} pid_side_t;

void pid_side_reset(pid_side_t *st);

// Вихід мікшера борту (−1…+1, обрізається) → задана швидкість, об/хв.
float pid_target_from_mix(float mix);

// Плавний розгін: модуль заданої зростає не більше ніж на ramp_rpm_s × dt_s; зменшення модуля діє одразу;
// зміна знака проходить через нуль одразу, далі зростає з тим самим темпом. Повертає нову задану після розгону.
float pid_ramp(float current_rpm, float requested_rpm, float ramp_rpm_s, float dt_s);

// Джерело швидкості борту за доступністю двох його коліс.
typedef enum { PID_SRC_TWO = 0, PID_SRC_ONE, PID_SRC_NONE } pid_src_t;

// Швидкість борту: середнє доступних коліс, одне колесо або «немає» (*rpm = 0).
pid_src_t pid_side_speed(bool avail_a, float rpm_a, bool avail_b, float rpm_b, float *rpm);

// Один крок регулятора на новому знімку енкодерів. target_rpm — задана після розгону (зі знаком), speed_rpm —
// швидкість борту, dt_s — різниця міток інтервалів енкодерів (обмежується PID_DT_MIN_S…PID_DT_MAX_S), limit —
// межа потужності (0…1). Повертає вихід зі знаком цілі, |вихід| ≤ limit; борт, швидший за задану, отримує 0
// (без зворотної потужності). Нульова ціль чи зміна знака скидають стан борту.
float pid_side_step(pid_side_t *st, const pid_set_t *set, float target_rpm, float speed_rpm, float dt_s, float limit);

// ---------- Показ стану і трасування (spec AC-03, AC-05, «Вміст рядка трасування»; ADR-0003) ----------
// Режим борту: за двома колесами, за одним колесом, без регулятора (регулятор вимкнений чи не налаштований),
// без енкодерів (жодного доступного колеса — теж без регулятора, але без виміру).
typedef enum { PID_SIDE_TWO = 0, PID_SIDE_ONE, PID_SIDE_OPEN, PID_SIDE_NO_ENC } pid_side_mode_t;

typedef enum { PID_LEFT = 0, PID_RIGHT = 1 } pid_board_t;

// Стан одного борту в такті керування — для показу стану і кадру трасування.
typedef struct {
    pid_side_mode_t mode;
    const char *missing;   // назва недоступного колеса (PID_SIDE_ONE), рядок статичного часу життя; інакше NULL
    float target_rpm;      // задана після розгону (зі знаком)
    float speed_rpm;       // швидкість борту (0, якщо PID_SIDE_NO_ENC)
    float out;             // вихід регулятора, частка потужності зі знаком
    float mix;             // вихід мікшера (−1…+1)
} pid_side_view_t;

// Кадр трасування: середні за 5 тактів (100 мс), час від старту, режими бортів, ознака ARMED, номер кадру.
typedef struct {
    uint32_t t_ms;
    uint32_t seq;
    bool armed;
    pid_side_mode_t mode[2];
    const char *missing[2];
    float target_rpm[2], speed_rpm[2], out[2], mix[2];
} pid_trace_frame_t;

#define PID_TRACE_TICKS 5   // тактів керування на кадр трасування (5 × 20 мс = 100 мс)

typedef struct {
    float sum_target[2], sum_speed[2], sum_out[2], sum_mix[2];
    uint8_t n;
    uint32_t seq;
} pid_trace_acc_t;

void pid_trace_acc_reset(pid_trace_acc_t *a);

// Додає такт. Повертає true, коли накопичено PID_TRACE_TICKS тактів: тоді *frame містить середні (режими —
// останнього такту), номер кадру зростає, а суми скидаються.
bool pid_trace_acc_add(pid_trace_acc_t *a, const pid_side_view_t side[2], uint32_t t_ms, bool armed,
                       pid_trace_frame_t *frame);

// Рядок трасування (spec «Вміст рядка трасування»); повертає довжину, завжди завершує нулем (n > 0).
size_t pid_format_trace(const pid_trace_frame_t *f, char *buf, size_t n);

// Показ стану `pid` (spec AC-03): режим, чотири числа з позначкою «збережено»/«не збережено» (незадані — «—»),
// по борту режим, задана, швидкість і вихід. side == NULL — без рядків бортів.
// load — результат завантаження запису на старті: відкинутий чи непрочитаний запис називається (spec AC-07).
size_t pid_format_show(const pid_set_t *s, const pid_side_view_t side[2], pid_rec_result_t load, char *buf, size_t n);

// Позначка після запису набору (spec AC-08): ok — запис вдався, «збережено» ставиться, лише якщо набір і режим
// не змінилися відносно snap; !ok — збій сховища, «збережено» знімається, набір і режим діють далі без змін.
void pid_save_mark(pid_set_t *cur, const pid_set_t *snap, bool ok);

// Текст відповіді на збереження snap: err == NULL — успіх із переліком чисел, інакше збій із назвою помилки.
// Окремо від pid_save_mark, щоб HAL форматував текст поза критичною секцією. Повертає довжину.
size_t pid_format_save(const pid_set_t *snap, const char *err, char *buf, size_t n);
