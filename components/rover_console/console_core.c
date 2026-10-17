// Логіка консолі: збирання рядка, розбір команд, права за станом.
// Без заголовків ESP-IDF і FreeRTOS; дії виконує console_hal.
#include <string.h>

#include "console_core.h"

void console_init(console_t *c)
{
    memset(c, 0, sizeof(*c));
}

console_event_t console_feed(console_t *c, char ch)
{
    if (c->line_ready) {  // попередній рядок уже забрано
        c->len = 0;
        c->buf[0] = '\0';
        c->overflowed = false;
        c->line_ready = false;
    }

    if (ch == '\r' || ch == '\n') {
        c->line_ready = true;
        if (c->overflowed) {
            c->len = 0;
            c->buf[0] = '\0';
            return CONSOLE_EVENT_OVERFLOW;
        }
        return CONSOLE_EVENT_LINE;
    }
    if (ch == 0x08 || ch == 0x7F) {  // BS, DEL
        if (!c->overflowed && c->len > 0) {
            c->len--;
            c->buf[c->len] = '\0';
        }
        return CONSOLE_EVENT_NONE;
    }
    if (ch != '\t' && (unsigned char)ch < 0x20) {  // інші керівні символи ігноруємо
        return CONSOLE_EVENT_NONE;
    }
    if (c->overflowed) {
        return CONSOLE_EVENT_NONE;
    }
    if (c->len >= CONSOLE_LINE_MAX) {
        c->overflowed = true;  // рядок відкидається повністю, до кінця рядка
        return CONSOLE_EVENT_NONE;
    }
    c->buf[c->len++] = ch;
    c->buf[c->len] = '\0';
    return CONSOLE_EVENT_NONE;
}

const char *console_line(const console_t *c)
{
    return c->buf;
}

static bool is_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

console_cmd_t console_parse(const char *line, app_state_t state)
{
    const char *p = line;
    while (is_space(*p)) p++;
    if (*p == '\0') {
        return CONSOLE_CMD_EMPTY;
    }
    const char *start = p;
    while (*p != '\0' && !is_space(*p)) p++;
    size_t n = (size_t)(p - start);
    while (is_space(*p)) p++;
    bool has_args = (*p != '\0');

    console_cmd_t cmd;
    if (n == 4 && strncmp(start, "help", n) == 0) {
        cmd = CONSOLE_CMD_HELP;
    } else if (n == 6 && strncmp(start, "status", n) == 0) {
        cmd = CONSOLE_CMD_STATUS;
    } else if (n == 6 && strncmp(start, "reboot", n) == 0) {
        cmd = CONSOLE_CMD_REBOOT;
    } else if (n == 8 && strncmp(start, "wdt-test", n) == 0) {
        cmd = CONSOLE_CMD_WDT_TEST;
    } else {
        return CONSOLE_CMD_UNKNOWN;
    }

    if (has_args) {
        return CONSOLE_CMD_EXTRA_ARGS;
    }
    if (cmd == CONSOLE_CMD_REBOOT && state != APP_STATE_DISARMED && state != APP_STATE_BOOT) {
        return CONSOLE_CMD_DENIED_STATE;
    }
    if (cmd == CONSOLE_CMD_WDT_TEST && state != APP_STATE_DISARMED) {
        return CONSOLE_CMD_DENIED_STATE;
    }
    return cmd;
}

const char *console_help_text(void)
{
    return "Доступні команди:\n"
           "  help      — ця довідка\n"
           "  status    — стан, час роботи, вільна памʼять, причина скидання, сховище, історія переходів\n"
           "  reboot    — перезавантажити плату (лише в станах DISARMED і BOOT)\n"
           "  wdt-test  — перевірка сторожового таймера: навмисне зависання (лише в стані DISARMED)";
}

const char *console_msg_unknown(void)
{
    return "Невідома команда. Наберіть help, щоб побачити список команд.";
}

const char *console_msg_extra_args(void)
{
    return "Команда не приймає аргументів. Наберіть help, щоб побачити список команд.";
}

const char *console_msg_overflow(void)
{
    return "Рядок задовгий (максимум 64 символи) — відкинуто повністю, нічого не виконано.";
}

const char *console_msg_denied(console_cmd_t cmd)
{
    if (cmd == CONSOLE_CMD_REBOOT) {
        return "Команда reboot дозволена лише в станах DISARMED і BOOT.";
    }
    if (cmd == CONSOLE_CMD_WDT_TEST) {
        return "Команда wdt-test дозволена лише в стані DISARMED.";
    }
    return "Команду відхилено.";
}
