// Логіка консолі: збирання рядка, розбір команд, права за станом.
// Без заголовків ESP-IDF і FreeRTOS; дії виконує console_hal.
#include <stdio.h>
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

    bool after_cr = c->last_was_cr;
    c->last_was_cr = (ch == '\r');
    if (ch == '\n' && after_cr) {  // другий символ пари CR LF
        return CONSOLE_EVENT_NONE;
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

// Розбирає перше слово: повертає команду, а в *rest — початок решти рядка (після пробілів).
static console_cmd_t first_word(const char *line, const char **rest)
{
    const char *p = line;
    while (is_space(*p)) p++;
    if (*p == '\0') {
        *rest = p;
        return CONSOLE_CMD_EMPTY;
    }
    const char *start = p;
    while (*p != '\0' && !is_space(*p)) p++;
    size_t n = (size_t)(p - start);
    while (is_space(*p)) p++;
    *rest = p;

    if (n == 4 && strncmp(start, "help", n) == 0) return CONSOLE_CMD_HELP;
    if (n == 6 && strncmp(start, "status", n) == 0) return CONSOLE_CMD_STATUS;
    if (n == 6 && strncmp(start, "reboot", n) == 0) return CONSOLE_CMD_REBOOT;
    if (n == 8 && strncmp(start, "wdt-test", n) == 0) return CONSOLE_CMD_WDT_TEST;
    if (n == 3 && strncmp(start, "arm", n) == 0) return CONSOLE_CMD_ARM;
    if (n == 6 && strncmp(start, "disarm", n) == 0) return CONSOLE_CMD_DISARM;
    if (n == 5 && strncmp(start, "drive", n) == 0) return CONSOLE_CMD_DRIVE;
    return CONSOLE_CMD_UNKNOWN;
}

console_cmd_t console_identify(const char *line)
{
    const char *rest;
    return first_word(line, &rest);
}

console_cmd_t console_parse(const char *line, app_state_t state)
{
    const char *rest;
    console_cmd_t cmd = first_word(line, &rest);
    if (cmd == CONSOLE_CMD_EMPTY || cmd == CONSOLE_CMD_UNKNOWN) {
        return cmd;
    }
    bool has_args = (*rest != '\0');

    if (cmd == CONSOLE_CMD_DRIVE) {  // аргументи перевіряються раніше за право за станом
        float t, s;
        if (console_parse_drive(line, &t, &s) != CONSOLE_DRIVE_OK) {
            return CONSOLE_CMD_BAD_ARGS;
        }
        return state == APP_STATE_ARMED ? CONSOLE_CMD_DRIVE : CONSOLE_CMD_DENIED_STATE;
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
    if (cmd == CONSOLE_CMD_ARM && state != APP_STATE_DISARMED) {
        return CONSOLE_CMD_DENIED_STATE;
    }
    return cmd;  // disarm дозволена в усіх станах
}

const char *console_help_text(void)
{
    return "Доступні команди:\n"
           "  help      — ця довідка\n"
           "  status    — стан, час роботи, вільна памʼять, причина скидання, сховище, історія переходів\n"
           "  reboot    — перезавантажити плату (лише в станах DISARMED і BOOT)\n"
           "  wdt-test  — перевірка сторожового таймера: навмисне зависання (лише в стані DISARMED)\n"
           "  arm       — озброїти ровер (лише в стані DISARMED за напруги не нижче порога)\n"
           "  disarm    — вимкнути мотори й роззброїти (у будь-якому стані)\n"
           "  drive <тяга> <поворот> — рух, значення від -1 до 1, діє 2 с (лише в стані ARMED)";
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

// ---------- Команди руху ----------

// Розбирає один токен за граматикою [+-]цифри[.цифри]; повертає false, якщо граматика порушена.
static bool parse_number(const char *tok, size_t n, double *out)
{
    size_t i = 0;
    bool neg = false;
    if (i < n && (tok[i] == '+' || tok[i] == '-')) {
        neg = tok[i] == '-';
        i++;
    }
    size_t int_start = i;
    double v = 0.0;
    while (i < n && tok[i] >= '0' && tok[i] <= '9') {
        v = v * 10.0 + (tok[i] - '0');
        i++;
    }
    if (i == int_start) {
        return false;
    }
    if (i < n && tok[i] == '.') {
        i++;
        size_t frac_start = i;
        double scale = 0.1;
        while (i < n && tok[i] >= '0' && tok[i] <= '9') {
            v += (tok[i] - '0') * scale;
            scale /= 10.0;
            i++;
        }
        if (i == frac_start) {
            return false;
        }
    }
    if (i != n) {
        return false;
    }
    *out = neg ? -v : v;
    return true;
}

console_drive_result_t console_parse_drive(const char *line, float *throttle, float *steer)
{
    const char *p = line;
    while (is_space(*p)) p++;
    while (*p != '\0' && !is_space(*p)) p++;  // слово drive

    const char *tok[3];
    size_t len[3];
    int count = 0;
    for (;;) {
        while (is_space(*p)) p++;
        if (*p == '\0') {
            break;
        }
        const char *start = p;
        while (*p != '\0' && !is_space(*p)) p++;
        if (count < 3) {
            tok[count] = start;
            len[count] = (size_t)(p - start);
        }
        count++;
    }
    if (count < 2) {
        return CONSOLE_DRIVE_MISSING_ARGS;
    }
    if (count > 2) {
        return CONSOLE_DRIVE_EXTRA_ARGS;
    }
    double a, b;
    if (!parse_number(tok[0], len[0], &a) || !parse_number(tok[1], len[1], &b)) {
        return CONSOLE_DRIVE_BAD_NUMBER;
    }
    if (a < -1.0 || a > 1.0 || b < -1.0 || b > 1.0) {
        return CONSOLE_DRIVE_RANGE;
    }
    *throttle = (float)a;
    *steer = (float)b;
    return CONSOLE_DRIVE_OK;
}

const char *console_msg_drive_error(console_drive_result_t r)
{
    switch (r) {
    case CONSOLE_DRIVE_MISSING_ARGS:
        return "Команда drive потребує двох чисел: drive <тяга> <поворот>.";
    case CONSOLE_DRIVE_EXTRA_ARGS:
        return "Забагато аргументів: drive приймає рівно два числа — тягу й поворот.";
    case CONSOLE_DRIVE_BAD_NUMBER:
        return "Значення має бути десятковим числом (наприклад 0.3 або -1); експонента, nan, inf, кома та зайві символи не приймаються.";
    case CONSOLE_DRIVE_RANGE:
        return "Значення мають бути в діапазоні від −1 до +1 (межі входять); команду відхилено.";
    case CONSOLE_DRIVE_OK:
        break;
    }
    return "Команду drive відхилено.";
}

void console_fmt_denied(char *buf, size_t n, console_cmd_t cmd, app_state_t state)
{
    const char *rule;
    switch (cmd) {
    case CONSOLE_CMD_ARM:      rule = "Команда arm дозволена лише в стані DISARMED"; break;
    case CONSOLE_CMD_DRIVE:    rule = "Команда drive дозволена лише в стані ARMED"; break;
    case CONSOLE_CMD_REBOOT:   rule = "Команда reboot дозволена лише в станах DISARMED і BOOT"; break;
    case CONSOLE_CMD_WDT_TEST: rule = "Команда wdt-test дозволена лише в стані DISARMED"; break;
    default:                   rule = "Команду відхилено"; break;
    }
    snprintf(buf, n, "%s, поточний стан: %s.%s", rule, app_state_name(state),
             state == APP_STATE_FAILSAFE
                 ? " Вихід із FAILSAFE можливий лише апаратним скиданням плати або перемиканням живлення."
                 : "");
}

void console_fmt_permille(char *buf, size_t n, int16_t permille)
{
    int v = permille < 0 ? -(int)permille : (int)permille;
    int hundredths = (v + 5) / 10;
    snprintf(buf, n, "%s%d.%02d", (permille < 0 && hundredths != 0) ? "-" : "", hundredths / 100, hundredths % 100);
}

void console_fmt_motor_status(char *buf, size_t n, uint32_t mv, bool mv_valid, int stby, int16_t left_pm,
                              int16_t right_pm, uint32_t remaining_ms)
{
    char l[16], r[16], v[48];
    console_fmt_permille(l, sizeof l, left_pm);
    console_fmt_permille(r, sizeof r, right_pm);
    if (mv_valid) {
        snprintf(v, sizeof v, "%u мВ", (unsigned)mv);
    } else {
        snprintf(v, sizeof v, "немає вимірювання");
    }
    snprintf(buf, n, "напруга: %s; STBY: %d; ліве: %s; праве: %s; залишок команди: %u мс", v, stby, l, r,
             (unsigned)remaining_ms);
}

void console_fmt_last_stop(char *buf, size_t n, const char *reason, uint32_t min_mv, uint32_t t_ms)
{
    if (reason == NULL) {
        snprintf(buf, n, "остання зупинка: немає");
        return;
    }
    char v[32];
    if (min_mv == 0) {
        snprintf(v, sizeof v, "немає даних");
    } else {
        snprintf(v, sizeof v, "%u мВ", (unsigned)min_mv);
    }
    snprintf(buf, n, "остання зупинка: %s; мінімальна напруга: %s; час: %u мс від старту", reason, v, (unsigned)t_ms);
}
