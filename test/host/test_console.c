// Тести логіки консолі (AC-02, AC-03, AC-05).
#include <string.h>

#include "console_core.h"
#include "drive_core.h"
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
    // CR теж завершує; LF одразу після CR (CR LF від монітора) ігнорується
    CHECK(feed_str(&c, "help", '\r') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "help") == 0);
    CHECK(console_feed(&c, '\n') == CONSOLE_EVENT_NONE);
    // LF сам по собі і два CR підряд — окремі (порожні) рядки
    CHECK(console_feed(&c, '\n') == CONSOLE_EVENT_LINE);
    CHECK(console_feed(&c, '\r') == CONSOLE_EVENT_LINE);
    CHECK(console_feed(&c, '\r') == CONSOLE_EVENT_LINE);
    // CR LF після рядка й наступна команда збирається чисто
    CHECK(feed_str(&c, "status", '\r') == CONSOLE_EVENT_LINE);
    CHECK(console_feed(&c, '\n') == CONSOLE_EVENT_NONE);
    CHECK(feed_str(&c, "help", '\r') == CONSOLE_EVENT_LINE);
    CHECK(strcmp(console_line(&c), "help") == 0);
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

// ---------- T5: arm, disarm, drive, розбір чисел, матриця дозволів, формати ----------
static void test_matrix_49(void)
{
    // Очікування за матрицею з spec §5: 7 команд × 7 станів
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        app_state_t a = (app_state_t)st;
        CHECK(console_parse("help", a) == CONSOLE_CMD_HELP);
        CHECK(console_parse("status", a) == CONSOLE_CMD_STATUS);
        CHECK(console_parse("disarm", a) == CONSOLE_CMD_DISARM);                   // дозволена в усіх станах
        CHECK(console_parse("arm", a) == (st == APP_STATE_DISARMED ? CONSOLE_CMD_ARM : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("drive 0.3 0", a) == (st == APP_STATE_ARMED ? CONSOLE_CMD_DRIVE : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("reboot", a) == ((st == APP_STATE_DISARMED || st == APP_STATE_BOOT) ? CONSOLE_CMD_REBOOT : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("wdt-test", a) == (st == APP_STATE_DISARMED ? CONSOLE_CMD_WDT_TEST : CONSOLE_CMD_DENIED_STATE));
    }
}

static void test_arm_disarm_args(void)
{
    CHECK(console_parse("arm now", APP_STATE_DISARMED) == CONSOLE_CMD_EXTRA_ARGS);
    CHECK(console_parse("disarm 1", APP_STATE_ARMED) == CONSOLE_CMD_EXTRA_ARGS);
    CHECK(console_identify("arm") == CONSOLE_CMD_ARM);
    CHECK(console_identify("disarm") == CONSOLE_CMD_DISARM);
    CHECK(console_identify("drive 1 0") == CONSOLE_CMD_DRIVE);
    CHECK(console_identify("ARM") == CONSOLE_CMD_UNKNOWN);
}

static void check_drive(const char *line, console_drive_result_t want, float t, float st)
{
    float a = 99.0f, b = 99.0f;
    console_drive_result_t r = console_parse_drive(line, &a, &b);
    CHECK(r == want);
    if (want == CONSOLE_DRIVE_OK) {
        CHECK(a > t - 0.0001f && a < t + 0.0001f);
        CHECK(b > st - 0.0001f && b < st + 0.0001f);
    } else {
        CHECK(a == 99.0f && b == 99.0f);   // при відмові вихідні значення не змінюються
    }
}

static void test_drive_grammar(void)
{
    check_drive("drive 0.3 0", CONSOLE_DRIVE_OK, 0.3f, 0.0f);
    check_drive("drive  1  -1 ", CONSOLE_DRIVE_OK, 1.0f, -1.0f);
    check_drive("drive -1 1", CONSOLE_DRIVE_OK, -1.0f, 1.0f);   // межі входять
    check_drive("drive +0.5 -0", CONSOLE_DRIVE_OK, 0.5f, 0.0f);
    check_drive("drive 0 0.40", CONSOLE_DRIVE_OK, 0.0f, 0.4f);
    check_drive("drive 1.5 0", CONSOLE_DRIVE_RANGE, 0, 0);
    check_drive("drive 0 -1.01", CONSOLE_DRIVE_RANGE, 0, 0);
    check_drive("drive 1.0000001 0", CONSOLE_DRIVE_RANGE, 0, 0);
    check_drive("drive nan 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 0 inf", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 1e0 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 0x1p-1 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 0,3 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 0.3abc 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 0.3. 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive +.3 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive .3 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 3. 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive - 0", CONSOLE_DRIVE_BAD_NUMBER, 0, 0);
    check_drive("drive 0.3", CONSOLE_DRIVE_MISSING_ARGS, 0, 0);
    check_drive("drive", CONSOLE_DRIVE_MISSING_ARGS, 0, 0);
    check_drive("drive 0.3 0 0", CONSOLE_DRIVE_EXTRA_ARGS, 0, 0);
    // console_parse віддає BAD_ARGS (перевірка аргументів передує праву за станом)
    CHECK(console_parse("drive 1.5 0", APP_STATE_ARMED) == CONSOLE_CMD_BAD_ARGS);
    CHECK(console_parse("drive nan 0", APP_STATE_DISARMED) == CONSOLE_CMD_BAD_ARGS);
    CHECK(console_parse("drive 0.3", APP_STATE_ARMED) == CONSOLE_CMD_BAD_ARGS);
    CHECK(console_parse("drive 0.3 0 0", APP_STATE_ARMED) == CONSOLE_CMD_BAD_ARGS);
}

static void test_fmt(void)
{
    char b[32];
    console_fmt_permille(b, sizeof b, 300);  CHECK(strcmp(b, "0.30") == 0);
    console_fmt_permille(b, sizeof b, 231);  CHECK(strcmp(b, "0.23") == 0);
    console_fmt_permille(b, sizeof b, 750);  CHECK(strcmp(b, "0.75") == 0);
    console_fmt_permille(b, sizeof b, -400); CHECK(strcmp(b, "-0.40") == 0);
    console_fmt_permille(b, sizeof b, 0);    CHECK(strcmp(b, "0.00") == 0);
    console_fmt_permille(b, sizeof b, 235);  CHECK(strcmp(b, "0.24") == 0);   // до найближчого
    console_fmt_permille(b, sizeof b, -4);   CHECK(strcmp(b, "0.00") == 0);   // без «−0.00»
    console_fmt_motor_status(b, 4, 8231, true, 0, 0, 0, 0);                    // малий буфер не падає
    char s[160];
    console_fmt_motor_status(s, sizeof s, 8231, true, 1, 300, -300, 1500);
    CHECK(strstr(s, "8231 мВ") && strstr(s, "STBY: 1") && strstr(s, "0.30") && strstr(s, "-0.30") && strstr(s, "1500 мс"));
    console_fmt_motor_status(s, sizeof s, 0, false, 0, 0, 0, 0);
    CHECK(strstr(s, "немає вимірювання") && strstr(s, "STBY: 0") && strstr(s, "0 мс") && strstr(s, "0.00"));
}

static void test_new_messages(void)
{
    CHECK(strstr(console_help_text(), "arm") && strstr(console_help_text(), "disarm") && strstr(console_help_text(), "drive"));
    char b[320];
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_DRIVE, APP_STATE_DISARMED);
    CHECK(strstr(b, "ARMED") && strstr(b, "DISARMED"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_ARM, APP_STATE_ARMED);
    CHECK(strstr(b, "DISARMED") && strstr(b, "ARMED"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_ARM, APP_STATE_FAILSAFE);
    CHECK(strstr(b, "FAILSAFE") && strstr(b, "апаратн"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_REBOOT, APP_STATE_ARMED);
    CHECK(strstr(b, "ARMED") && strstr(b, "DISARMED") && strstr(b, "BOOT"));
    CHECK(strstr(console_msg_drive_error(CONSOLE_DRIVE_RANGE), "−1") != NULL);
    CHECK(strlen(console_msg_drive_error(CONSOLE_DRIVE_BAD_NUMBER)) > 10);
}

static void test_last_stop(void)
{
    char b[200];
    console_fmt_last_stop(b, sizeof b, NULL, 0, 0);
    CHECK(strstr(b, "остання зупинка: немає") != NULL);
    console_fmt_last_stop(b, sizeof b, "команда руху спливла", 7729, 297639);
    CHECK(strstr(b, "остання зупинка: команда руху спливла") != NULL);
    CHECK(strstr(b, "7729 мВ") != NULL && strstr(b, "297639 мс") != NULL);
    console_fmt_last_stop(b, sizeof b, "роззброєно командою disarm", 0, 5);
    CHECK(strstr(b, "мінімальна напруга: немає даних") != NULL);
    console_fmt_last_stop(b, 6, "x", 1, 1);   // малий буфер не падає
    CHECK(strlen(b) <= 5);
}

static void test_last_stop_real_texts(void)
{
    // Справжні тексти причин із максимальними числами вміщаються в буфер console_hal (320 байтів) без обрізання
    char b[320];
    drive_stop_t all[] = {DRIVE_STOP_WINDOW_EXPIRED, DRIVE_STOP_LOW_VOLTAGE, DRIVE_STOP_STALE_VOLTAGE, DRIVE_STOP_DISARM};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        console_fmt_last_stop(b, sizeof b, drive_stop_text(all[i]), 4294967295u, 4294967295u);
        CHECK(strstr(b, "мс від старту") != NULL);   // хвіст на місці: рядок не обрізано
        CHECK(strlen(b) < 300);
    }
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
    test_matrix_49();
    test_arm_disarm_args();
    test_drive_grammar();
    test_fmt();
    test_new_messages();
    test_last_stop();
    test_last_stop_real_texts();
    return test_summary();
}
