// Чиста логіка консолі (spec AC-02, AC-03, AC-05): збирання рядка, розбір команд, права за станом.
// Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"
#include "ui_core.h"

#define CONSOLE_LINE_MAX 64  // максимум символів у рядку команди (без завершального нуля)

typedef enum {
    CONSOLE_EVENT_NONE,
    CONSOLE_EVENT_LINE,      // рядок готовий: console_line()
    CONSOLE_EVENT_OVERFLOW   // рядок довший за CONSOLE_LINE_MAX відкинуто повністю
} console_event_t;

typedef struct {
    char buf[CONSOLE_LINE_MAX + 1];
    size_t len;
    bool overflowed;   // рядок вже перевищив ліміт: решту до кінця рядка відкидаємо
    bool line_ready;   // після LINE/OVERFLOW: наступний символ починає новий рядок
    bool last_was_cr;  // попередній символ — CR: наступний LF (CR LF від монітора) ігнорується
} console_t;

typedef enum {
    CONSOLE_CMD_EMPTY,
    CONSOLE_CMD_UNKNOWN,
    CONSOLE_CMD_EXTRA_ARGS,
    CONSOLE_CMD_DENIED_STATE,
    CONSOLE_CMD_HELP,
    CONSOLE_CMD_STATUS,
    CONSOLE_CMD_REBOOT,
    CONSOLE_CMD_WDT_TEST,
    CONSOLE_CMD_ARM,
    CONSOLE_CMD_DISARM,
    CONSOLE_CMD_DRIVE,
    CONSOLE_CMD_LIGHT,
    CONSOLE_CMD_BEEP,
    CONSOLE_CMD_BAD_ARGS,  // drive, light, beep чи enc із відсутніми, зайвими, нечисловими чи позамежними аргументами
    CONSOLE_CMD_ENC,       // знімок енкодерів (phase4-encoders, contracts/cli.md)
    CONSOLE_CMD_ENC_RESET  // обнулення лічильників енкодерів
} console_cmd_t;

void console_init(console_t *c);

// Подає один байт вводу. CR або LF завершують рядок (LF одразу після CR ігнорується — монітор шле CR LF); BS (0x08) і DEL (0x7F) стирають символ;
// керівні символи ігноруються. Після LINE рядок доступний через console_line() до наступного виклику.
console_event_t console_feed(console_t *c, char ch);
const char *console_line(const console_t *c);

// Розбір рядка. Невідома команда має пріоритет над аргументами; зайві аргументи — над правами за станом.
console_cmd_t console_parse(const char *line, app_state_t state);

// Яку команду названо в рядку, без урахування аргументів і прав за станом
// (потрібно HAL, щоб пояснити відмову DENIED_STATE). Невідома — UNKNOWN, порожній — EMPTY.
console_cmd_t console_identify(const char *line);

// Тексти відповідей (українською).
const char *console_help_text(void);
const char *console_msg_unknown(void);
const char *console_msg_extra_args(void);
const char *console_msg_overflow(void);
// Розбір команди enc (contracts/cli.md): дві допустимі форми і три причини хибної.
typedef enum {
    CONSOLE_ENC_SNAPSHOT,      // «enc»
    CONSOLE_ENC_RESET,         // «enc reset»
    CONSOLE_ENC_UNKNOWN_WORD,  // невідоме друге слово
    CONSOLE_ENC_RESET_TYPO,    // помилка в reset: інший регістр, обрізане чи подовжене слово (Reset, rese, resetx)
    CONSOLE_ENC_EXTRA_WORD     // третє слово
} console_enc_result_t;

console_enc_result_t console_parse_enc(const char *line);
// Пояснення хибної форми команди enc: що саме не так і обидві допустимі форми (spec AC-07).
const char *console_msg_enc_error(console_enc_result_t r);
// Пояснення відмови за станом для команди REBOOT або WDT_TEST.
const char *console_msg_denied(console_cmd_t cmd);

// ---------- Команди руху (spec AC-05, AC-06, AC-07, AC-16) ----------

typedef enum {
    CONSOLE_DRIVE_OK,
    CONSOLE_DRIVE_MISSING_ARGS,
    CONSOLE_DRIVE_EXTRA_ARGS,
    CONSOLE_DRIVE_BAD_NUMBER,   // не за граматикою: [+-]цифри[.цифри]
    CONSOLE_DRIVE_RANGE         // число коректне, але поза [−1, +1]
} console_drive_result_t;

// Розбір рядка «drive <тяга> <поворот>». Значення через *throttle і *steer змінюються лише при CONSOLE_DRIVE_OK.
console_drive_result_t console_parse_drive(const char *line, float *throttle, float *steer);
const char *console_msg_drive_error(console_drive_result_t r);

// Пояснення відмови за станом: називає поточний стан; у FAILSAFE підказує вихід за причиною (cause):
// після втрати звʼязку — пультом або скиданням, інакше лише апаратним скиданням.
void console_fmt_denied(char *buf, size_t n, console_cmd_t cmd, app_state_t state, app_failsafe_cause_t cause);

// Успішна відповідь arm. Консоль озброює лише в стендовому режимі, тож джерело завжди «стенд» (contracts/cli.md, AC-13).
const char *console_msg_arm_ok(void);

// Значення борту в проміле зі знаком → «0.30» (два знаки, округлення до найближчого, без «-0.00»).
void console_fmt_permille(char *buf, size_t n, int16_t permille);

// Рядок про живлення і мотори для status (spec AC-16).
void console_fmt_motor_status(char *buf, size_t n, uint32_t mv, bool mv_valid, int stby, int16_t left_pm,
                              int16_t right_pm, uint32_t remaining_ms);

// Рядок про останню зупинку моторів для status (spec AC-12): reason == NULL — зупинок ще не було;
// min_mv == 0 — напруга за час руху невідома.
void console_fmt_last_stop(char *buf, size_t n, const char *reason, uint32_t min_mv, uint32_t t_ms);

// ---------- Команди світла й зумера (spec AC-05, AC-06, AC-07, AC-10, AC-13; contracts/cli.md) ----------

typedef enum {
    CONSOLE_UI_OK,
    CONSOLE_UI_MISSING_ARGS, // аргументів бракує
    CONSOLE_UI_EXTRA_ARGS,   // зайві аргументи
    CONSOLE_UI_UNKNOWN_CHANNEL,  // light: невідомий канал (регістр має значення)
    CONSOLE_UI_UNKNOWN_ACTION,   // light: невідома дія (не on і не off)
    CONSOLE_UI_NOT_INTEGER,  // не лише десяткові цифри: знак, дріб, експонента, 0x…, кома, зайві символи
    CONSOLE_UI_RANGE         // ціле, але поза діапазоном зумера
} console_ui_result_t;

// Розбір «light <head|red|green> <on|off>». Вихідні значення змінюються лише при CONSOLE_UI_OK.
console_ui_result_t console_parse_light(const char *line, ui_channel_t *ch, bool *on);
// Розбір «beep <Гц> <мс>»: лише цифри; значення не округлюються й не обрізаються.
console_ui_result_t console_parse_beep(const char *line, uint32_t *hz, uint32_t *dur_ms);

const char *console_msg_light_error(console_ui_result_t r);
const char *console_msg_beep_error(console_ui_result_t r);
// Пояснення відмови ui_core: за станом (названо стан), задні ліхтарі керує рух, розширювач недоступний, діапазон.
void console_fmt_ui_denied(char *buf, size_t n, ui_result_t r, app_state_t state, app_failsafe_cause_t cause);

// Рядок про світло, зумер і індикатор для status (spec AC-13).
void console_fmt_ui_status(char *buf, size_t n, bool expander_ok, bool head, bool red, bool green, bool rear_auto,
                           bool indicator_red, bool buzzer_on, uint32_t remaining_ms);
