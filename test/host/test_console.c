// Тести логіки консолі (AC-02, AC-03, AC-05).
#include <string.h>

#include "console_core.h"
#include "test_support.h"

// Подає рядок s і завершувальний символ end; повертає останню подію.
static console_event_t feed_str(console_t *c, const char *s, char end)
{
    console_event_t ev = CONSOLE_EVENT_NONE;
    for (; *s; s++) {
        ev = console_feed(c, *s);
        if (ev != CONSOLE_EVENT_NONE) return ev;
    }
    return console_feed(c, end);
}

static void test_line_assembly(void)
{
    console_t c;
    console_init(&c);
    CHECK(console_feed(&c, 's') == CONSOLE_EVENT_NONE);
    CHECK(feed_str(&c, "tatus", '\n') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "status") == 0);
    // CR теж завершує; наступний LF дає порожній рядок
    CHECK(feed_str(&c, "help", '\r') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "help") == 0);
    CHECK(console_feed(&c, '\n') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "") == 0);
}

static void test_backspace(void)
{
    console_t c;
    console_init(&c);
    CHECK(feed_str(&c, "stax", 0x08) == CONSOLE_EVENT_NONE);
    CHECK(feed_str(&c, "tus", '\n') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "status") == 0);
    console_init(&c);
    CHECK(console_feed(&c, 0x7F) == CONSOLE_EVENT_NONE);  // стирання порожнього рядка безпечне
    CHECK(feed_str(&c, "help", '\n') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "help") == 0);
}

static void test_limit_64_ok_65_overflow(void)
{
    console_t c;
    char s[CONSOLE_LINE_MAX + 2];
    memset(s, 'a', sizeof(s));
    s[CONSOLE_LINE_MAX] = '\0';
    console_init(&c);
    CHECK(feed_str(&c, s, '\n') == CONSOLE_EVENT_LINE);       // рівно 64
    CHECK(strlen(console_line(&c)) == CONSOLE_LINE_MAX);
    s[CONSOLE_LINE_MAX] = 'a';
    s[CONSOLE_LINE_MAX + 1] = '\0';                           // 65
    console_init(&c);
    CHECK(feed_str(&c, s, '\n') == CONSOLE_EVENT_OVERFLOW);
    // після відкидання консоль працює: наступний рядок збирається чисто
    CHECK(feed_str(&c, "help", '\n') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "help") == 0);
}

static void test_overflow_discards_whole_line(void)
{
    console_t c;
    char s[100];
    memset(s, 'a', sizeof(s));
    s[99] = '\0';
    console_init(&c);
    // у переповненому рядку «help» наприкінці не повинен виконатися
    console_event_t ev = CONSOLE_EVENT_NONE;
    for (int i = 0; i < 99; i++) ev = console_feed(&c, s[i]);
    CHECK(ev == CONSOLE_EVENT_NONE);
    CHECK(feed_str(&c, " help", '\n') == CONSOLE_EVENT_OVERFLOW);
}

static void test_control_chars_ignored(void)
{
    console_t c;
    console_init(&c);
    CHECK(console_feed(&c, 0x01) == CONSOLE_EVENT_NONE);
    CHECK(console_feed(&c, '\0') == CONSOLE_EVENT_NONE);
    CHECK(feed_str(&c, "help", '\n') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "help") == 0);
}

static void test_parse_empty(void)
{
    CHECK(console_parse("", APP_STATE_DISARMED) == CONSOLE_CMD_EMPTY);
    CHECK(console_parse("   ", APP_STATE_DISARMED) == CONSOLE_CMD_EMPTY);
    CHECK(console_parse(" \t ", APP_STATE_BOOT) == CONSOLE_CMD_EMPTY);
}

static void test_parse_known_in_all_states(void)
{
    for (int s = 0; s < APP_STATE_COUNT; s++) {
        CHECK(console_parse("help", (app_state_t)s) == CONSOLE_CMD_HELP);
        CHECK(console_parse("status", (app_state_t)s) == CONSOLE_CMD_STATUS);
        CHECK(console_parse("  status  ", (app_state_t)s) == CONSOLE_CMD_STATUS);
    }
}

static void test_parse_unknown_and_case(void)
{
    CHECK(console_parse("foo", APP_STATE_DISARMED) == CONSOLE_CMD_UNKNOWN);
    CHECK(console_parse("HELP", APP_STATE_DISARMED) == CONSOLE_CMD_UNKNOWN);
    CHECK(console_parse("Status", APP_STATE_DISARMED) == CONSOLE_CMD_UNKNOWN);
    CHECK(console_parse("foo bar", APP_STATE_DISARMED) == CONSOLE_CMD_UNKNOWN);
}

static void test_parse_extra_args(void)
{
    CHECK(console_parse("status foo", APP_STATE_DISARMED) == CONSOLE_CMD_EXTRA_ARGS);
    CHECK(console_parse("help me", APP_STATE_DISARMED) == CONSOLE_CMD_EXTRA_ARGS);
    CHECK(console_parse("reboot now", APP_STATE_DISARMED) == CONSOLE_CMD_EXTRA_ARGS);
    CHECK(console_parse("wdt-test 1", APP_STATE_DISARMED) == CONSOLE_CMD_EXTRA_ARGS);
    // зайві аргументи розглядаються раніше за права
    CHECK(console_parse("reboot now", APP_STATE_ARMED) == CONSOLE_CMD_EXTRA_ARGS);
}

static void test_permissions_wdt_test(void)
{
    for (int s = 0; s < APP_STATE_COUNT; s++) {
        console_cmd_t r = console_parse("wdt-test", (app_state_t)s);
        if (s == APP_STATE_DISARMED) {
            CHECK(r == CONSOLE_CMD_WDT_TEST);
        } else {
            CHECK(r == CONSOLE_CMD_DENIED_STATE);
        }
    }
}

static void test_permissions_reboot(void)
{
    for (int s = 0; s < APP_STATE_COUNT; s++) {
        console_cmd_t r = console_parse("reboot", (app_state_t)s);
        if (s == APP_STATE_DISARMED || s == APP_STATE_BOOT) {
            CHECK(r == CONSOLE_CMD_REBOOT);
        } else {
            CHECK(r == CONSOLE_CMD_DENIED_STATE);
        }
    }
}

static void test_identify(void)
{
    CHECK(console_identify("") == CONSOLE_CMD_EMPTY);
    CHECK(console_identify("  ") == CONSOLE_CMD_EMPTY);
    CHECK(console_identify("foo") == CONSOLE_CMD_UNKNOWN);
    CHECK(console_identify("HELP") == CONSOLE_CMD_UNKNOWN);
    CHECK(console_identify("reboot") == CONSOLE_CMD_REBOOT);
    CHECK(console_identify("  wdt-test ") == CONSOLE_CMD_WDT_TEST);
    CHECK(console_identify("reboot now") == CONSOLE_CMD_REBOOT);  // аргументи не враховуються
    CHECK(console_identify("status") == CONSOLE_CMD_STATUS);
}

static void test_messages(void)
{
    CHECK(strstr(console_help_text(), "help") != NULL);
    CHECK(strstr(console_help_text(), "status") != NULL);
    CHECK(strstr(console_help_text(), "reboot") != NULL);
    CHECK(strstr(console_help_text(), "wdt-test") != NULL);
    CHECK(strstr(console_msg_unknown(), "help") != NULL);
    CHECK(strlen(console_msg_extra_args()) > 0);
    CHECK(strstr(console_msg_overflow(), "64") != NULL);
    CHECK(strstr(console_msg_denied(CONSOLE_CMD_WDT_TEST), "DISARMED") != NULL);
    CHECK(strstr(console_msg_denied(CONSOLE_CMD_REBOOT), "DISARMED") != NULL);
    CHECK(strstr(console_msg_denied(CONSOLE_CMD_REBOOT), "BOOT") != NULL);
}

int main(void)
{
    test_line_assembly();
    test_backspace();
    test_limit_64_ok_65_overflow();
    test_overflow_discards_whole_line();
    test_control_chars_ignored();
    test_parse_empty();
    test_parse_known_in_all_states();
    test_parse_unknown_and_case();
    test_parse_extra_args();
    test_permissions_wdt_test();
    test_permissions_reboot();
    test_identify();
    test_messages();
    return test_summary();
}
