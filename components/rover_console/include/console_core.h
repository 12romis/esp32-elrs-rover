// Чиста логіка консолі (spec AC-02, AC-03, AC-05): збирання рядка, розбір команд, права за станом.
// Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_state.h"

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
} console_t;

typedef enum {
    CONSOLE_CMD_EMPTY,
    CONSOLE_CMD_UNKNOWN,
    CONSOLE_CMD_EXTRA_ARGS,
    CONSOLE_CMD_DENIED_STATE,
    CONSOLE_CMD_HELP,
    CONSOLE_CMD_STATUS,
    CONSOLE_CMD_REBOOT,
    CONSOLE_CMD_WDT_TEST
} console_cmd_t;

void console_init(console_t *c);

// Подає один байт вводу. CR або LF завершують рядок; BS (0x08) і DEL (0x7F) стирають символ;
// керівні символи ігноруються. Після LINE рядок доступний через console_line() до наступного виклику.
console_event_t console_feed(console_t *c, char ch);
const char *console_line(const console_t *c);

// Розбір рядка. Невідома команда має пріоритет над аргументами; зайві аргументи — над правами за станом.
console_cmd_t console_parse(const char *line, app_state_t state);

// Тексти відповідей (українською).
const char *console_help_text(void);
const char *console_msg_unknown(void);
const char *console_msg_extra_args(void);
const char *console_msg_overflow(void);
// Пояснення відмови за станом для команди REBOOT або WDT_TEST.
const char *console_msg_denied(console_cmd_t cmd);
