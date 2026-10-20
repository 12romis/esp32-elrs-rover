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
    // phase3-crsf: arm і drive лише в стендовому режимі, disarm без обмежень (AC-15).
    {
        const char *h = console_help_text();
        const char *arm = strstr(h, "  arm ");
        const char *disarm = strstr(h, "  disarm ");
        const char *drive = strstr(h, "  drive ");
        CHECK(arm && disarm && drive);
        CHECK(strstr(arm, "стендовому режимі") != NULL && strstr(arm, "стендовому режимі") < disarm);
        CHECK(strstr(drive, "стендовому режимі") != NULL);
        CHECK(strstr(disarm, "у будь-якому стані") != NULL);
        CHECK(strstr(disarm, "стендовому") == NULL || strstr(disarm, "стендовому") > strstr(disarm, "\n  drive"));
    }
    char b[320];
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_DRIVE, APP_STATE_DISARMED, APP_FS_NONE);
    CHECK(strstr(b, "ARMED") && strstr(b, "DISARMED"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_ARM, APP_STATE_ARMED, APP_FS_NONE);
    CHECK(strstr(b, "DISARMED") && strstr(b, "ARMED"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_ARM, APP_STATE_FAILSAFE, APP_FS_VOLTAGE);
    CHECK(strstr(b, "FAILSAFE") && strstr(b, "апаратн"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_REBOOT, APP_STATE_ARMED, APP_FS_NONE);
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


// ---------- Фаза 2: light і beep (spec AC-05, AC-06, AC-07, AC-10, AC-13; contracts/cli.md) ----------
static void test_matrix_63(void)
{
    // 9 команд × 7 станів = 63 пари; для light і beep — лише DISARMED і ARMED
    int pairs = 0;
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        app_state_t a = (app_state_t)st;
        bool ui_ok = (st == APP_STATE_DISARMED || st == APP_STATE_ARMED);
        CHECK(console_parse("help", a) == CONSOLE_CMD_HELP);
        CHECK(console_parse("status", a) == CONSOLE_CMD_STATUS);
        CHECK(console_parse("disarm", a) == CONSOLE_CMD_DISARM);
        CHECK(console_parse("arm", a) == (st == APP_STATE_DISARMED ? CONSOLE_CMD_ARM : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("drive 0.3 0", a) == (st == APP_STATE_ARMED ? CONSOLE_CMD_DRIVE : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("reboot", a) == ((st == APP_STATE_DISARMED || st == APP_STATE_BOOT) ? CONSOLE_CMD_REBOOT : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("wdt-test", a) == (st == APP_STATE_DISARMED ? CONSOLE_CMD_WDT_TEST : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("light head on", a) == (ui_ok ? CONSOLE_CMD_LIGHT : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("beep 1000 100", a) == (ui_ok ? CONSOLE_CMD_BEEP : CONSOLE_CMD_DENIED_STATE));
        pairs += 9;
    }
    CHECK(pairs == 63);
    printf("ПОКРИТТЯ пар команда × стан: %d з 63\n", pairs);
    // для клітинок «ні» причина — завжди стан, хай які аргументи
    CHECK(console_parse("light foo", APP_STATE_FAILSAFE) == CONSOLE_CMD_DENIED_STATE);
    CHECK(console_parse("beep abc", APP_STATE_BOOT) == CONSOLE_CMD_DENIED_STATE);
    CHECK(console_parse("light", APP_STATE_FAULT) == CONSOLE_CMD_DENIED_STATE);
    CHECK(console_parse("beep 1 2 3", APP_STATE_OBSTACLE_HOLD) == CONSOLE_CMD_DENIED_STATE);
    // дозволений стан: далі синтаксис
    CHECK(console_parse("light foo", APP_STATE_DISARMED) == CONSOLE_CMD_BAD_ARGS);
    CHECK(console_parse("beep abc 1", APP_STATE_ARMED) == CONSOLE_CMD_BAD_ARGS);
    CHECK(console_identify("light head on") == CONSOLE_CMD_LIGHT);
    CHECK(console_identify("beep 1000 100") == CONSOLE_CMD_BEEP);
}

static void check_light(const char *line, console_ui_result_t want, ui_channel_t ch, bool on)
{
    ui_channel_t c = UI_CH_COUNT;
    bool o = !on;
    console_ui_result_t r = console_parse_light(line, &c, &o);
    CHECK(r == want);
    if (want == CONSOLE_UI_OK) {
        CHECK(c == ch && o == on);
    } else {
        CHECK(c == UI_CH_COUNT && o == !on);  // при відмові значення не змінюються
    }
}

static void test_light_grammar(void)
{
    check_light("light head on", CONSOLE_UI_OK, UI_CH_HEAD, true);
    check_light("light red off", CONSOLE_UI_OK, UI_CH_RED, false);
    check_light("  light   green   on  ", CONSOLE_UI_OK, UI_CH_GREEN, true);
    check_light("light", CONSOLE_UI_MISSING_ARGS, UI_CH_COUNT, false);
    check_light("light head", CONSOLE_UI_MISSING_ARGS, UI_CH_COUNT, false);
    check_light("light foo on", CONSOLE_UI_UNKNOWN_CHANNEL, UI_CH_COUNT, false);
    check_light("light head maybe", CONSOLE_UI_UNKNOWN_ACTION, UI_CH_COUNT, false);
    check_light("light head on now", CONSOLE_UI_EXTRA_ARGS, UI_CH_COUNT, false);
    check_light("light HEAD on", CONSOLE_UI_UNKNOWN_CHANNEL, UI_CH_COUNT, false);   // регістр має значення
    check_light("light head ON", CONSOLE_UI_UNKNOWN_ACTION, UI_CH_COUNT, false);
}

static void check_beep(const char *line, console_ui_result_t want, uint32_t hz, uint32_t dur)
{
    uint32_t h = 7, d = 7;
    console_ui_result_t r = console_parse_beep(line, &h, &d);
    CHECK(r == want);
    if (want == CONSOLE_UI_OK) {
        CHECK(h == hz && d == dur);
    } else {
        CHECK(h == 7 && d == 7);
    }
}

static void test_beep_grammar(void)
{
    check_beep("beep 1000 300", CONSOLE_UI_OK, 1000, 300);
    check_beep("beep 200 1", CONSOLE_UI_OK, 200, 1);          // межі входять
    check_beep("beep 5000 2000", CONSOLE_UI_OK, 5000, 2000);
    check_beep("beep 0001000 0300", CONSOLE_UI_OK, 1000, 300);  // нулі спереду — все одно цифри
    check_beep("beep 199 100", CONSOLE_UI_RANGE, 0, 0);
    check_beep("beep 5001 100", CONSOLE_UI_RANGE, 0, 0);
    check_beep("beep 1000 0", CONSOLE_UI_RANGE, 0, 0);
    check_beep("beep 1000 2001", CONSOLE_UI_RANGE, 0, 0);
    check_beep("beep 0 100", CONSOLE_UI_RANGE, 0, 0);
    check_beep("beep 100000000000 100", CONSOLE_UI_RANGE, 0, 0);  // переповнення читається як «поза діапазоном»
    check_beep("beep -1 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep +1000 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep 1.5 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep 1e3 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep 0x10 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep 1,5 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep 1000x 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep abc 100", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep 1000 1.5", CONSOLE_UI_NOT_INTEGER, 0, 0);
    check_beep("beep", CONSOLE_UI_MISSING_ARGS, 0, 0);
    check_beep("beep 1000", CONSOLE_UI_MISSING_ARGS, 0, 0);
    check_beep("beep 1000 100 5", CONSOLE_UI_EXTRA_ARGS, 0, 0);
    check_beep("beep 99999 abc", CONSOLE_UI_NOT_INTEGER, 0, 0);  // формат важливіший за діапазон
}

// T15 (review F2): у FAILSAFE відмова за станом пояснює вихід за причиною (contracts/cli.md, клас 1; AC-08, AC-09).
static void test_failsafe_denied_by_cause(void)
{
    char b[320];  // як буфер console_hal
    const console_cmd_t cmds[] = {CONSOLE_CMD_ARM, CONSOLE_CMD_DRIVE, CONSOLE_CMD_REBOOT, CONSOLE_CMD_WDT_TEST,
                                  CONSOLE_CMD_LIGHT, CONSOLE_CMD_BEEP};
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        console_fmt_denied(b, sizeof b, cmds[i], APP_STATE_FAILSAFE, APP_FS_LINK_LOST);
        CHECK(strstr(b, "FAILSAFE") && strstr(b, "пульт") && strstr(b, "DISARM") && strstr(b, "апаратн"));
        CHECK(strstr(b, "лише апаратним") == NULL);
        CHECK(strlen(b) < sizeof b - 1);
        console_fmt_denied(b, sizeof b, cmds[i], APP_STATE_FAILSAFE, APP_FS_VOLTAGE);
        CHECK(strstr(b, "лише апаратним") && strstr(b, "пульт") == NULL);
    }
    char w[400];  // як буфер console_hal
    console_fmt_ui_denied(w, sizeof w, UI_DENY_STATE, APP_STATE_FAILSAFE, APP_FS_LINK_LOST);
    CHECK(strstr(w, "пульт") && strstr(w, "DISARM") && strstr(w, "лише апаратним") == NULL && strlen(w) < sizeof w - 1);
    console_fmt_ui_denied(w, sizeof w, UI_DENY_STATE, APP_STATE_FAILSAFE, APP_FS_VOLTAGE);
    CHECK(strstr(w, "лише апаратним") != NULL);
    // Поза FAILSAFE підказки про вихід немає.
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_ARM, APP_STATE_ARMED, APP_FS_NONE);
    CHECK(strstr(b, "Вихід із FAILSAFE") == NULL);
}

// T16 (review F3): успішна відповідь arm (можлива лише в стенді) називає джерело керування «стенд» (AC-13).
static void test_arm_ok_names_bench(void)
{
    const char *m = console_msg_arm_ok();
    CHECK(strstr(m, "озброєно") && strstr(m, "джерело керування: стенд") && strstr(m, "drive"));
}

static void test_ui_messages(void)
{
    CHECK(strstr(console_help_text(), "light") && strstr(console_help_text(), "beep"));
    CHECK(strstr(console_help_text(), "head") && strstr(console_help_text(), "on"));
    char b[400];  // як буфер console_hal
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_LIGHT, APP_STATE_FAILSAFE, APP_FS_VOLTAGE);
    CHECK(strstr(b, "DISARMED") && strstr(b, "ARMED") && strstr(b, "FAILSAFE"));
    console_fmt_denied(b, sizeof b, CONSOLE_CMD_BEEP, APP_STATE_BOOT, APP_FS_NONE);
    CHECK(strstr(b, "DISARMED") && strstr(b, "BOOT"));
    // кожна причина названа окремо (AC-05) і з переліком допустимого
    CHECK(strstr(console_msg_light_error(CONSOLE_UI_MISSING_ARGS), "бракує"));
    CHECK(strstr(console_msg_light_error(CONSOLE_UI_EXTRA_ARGS), "зайві"));
    CHECK(strstr(console_msg_light_error(CONSOLE_UI_UNKNOWN_CHANNEL), "канал") && strstr(console_msg_light_error(CONSOLE_UI_UNKNOWN_CHANNEL), "head"));
    CHECK(strstr(console_msg_light_error(CONSOLE_UI_UNKNOWN_ACTION), "дія") && strstr(console_msg_light_error(CONSOLE_UI_UNKNOWN_ACTION), "off"));
    for (int r = CONSOLE_UI_MISSING_ARGS; r <= CONSOLE_UI_UNKNOWN_ACTION; r++) {
        CHECK(strstr(console_msg_light_error((console_ui_result_t)r), "head") && strstr(console_msg_light_error((console_ui_result_t)r), "on"));
    }
    CHECK(strcmp(console_msg_light_error(CONSOLE_UI_MISSING_ARGS), console_msg_light_error(CONSOLE_UI_EXTRA_ARGS)) != 0);
    CHECK(strstr(console_msg_beep_error(CONSOLE_UI_RANGE), "200") && strstr(console_msg_beep_error(CONSOLE_UI_RANGE), "5000"));
    CHECK(strstr(console_msg_beep_error(CONSOLE_UI_RANGE), "2000") != NULL);
    CHECK(strlen(console_msg_beep_error(CONSOLE_UI_NOT_INTEGER)) > 10);
    CHECK(strstr(console_msg_beep_error(CONSOLE_UI_MISSING_ARGS), "бракує"));
    CHECK(strstr(console_msg_beep_error(CONSOLE_UI_EXTRA_ARGS), "зайві"));
    console_fmt_ui_denied(b, sizeof b, UI_DENY_STATE, APP_STATE_FAULT, APP_FS_NONE);
    CHECK(strstr(b, "FAULT") && strstr(b, "DISARMED"));
    console_fmt_ui_denied(b, sizeof b, UI_DENY_MOTION, APP_STATE_ARMED, APP_FS_NONE);
    CHECK(strstr(b, "рух") && strstr(b, "DISARMED"));
    console_fmt_ui_denied(b, sizeof b, UI_DENY_EXPANDER, APP_STATE_DISARMED, APP_FS_NONE);
    CHECK(strstr(b, "розширювач") && strstr(b, "недоступн") && strstr(b, "перезапуск"));  // текст не обрізано
    CHECK(strlen(b) < 380);
    console_fmt_ui_denied(b, sizeof b, UI_DENY_STATE, APP_STATE_FAILSAFE, APP_FS_VOLTAGE);
    CHECK(strstr(b, "апаратн") && strlen(b) < 380);
    console_fmt_ui_denied(b, sizeof b, UI_DENY_RANGE, APP_STATE_DISARMED, APP_FS_NONE);
    CHECK(strstr(b, "200") && strstr(b, "5000"));
    char s[320];  // як буфер console_hal
    console_fmt_ui_status(s, sizeof s, true, true, false, true, true, false, true, 250);
    CHECK(strstr(s, "розширювач: доступний") && strstr(s, "head") && strstr(s, "ввімкнено") && strstr(s, "рух"));
    CHECK(strstr(s, "індикатор плати: вимкнений") && strstr(s, "зумер: звучить") && strstr(s, "250 мс"));
    console_fmt_ui_status(s, sizeof s, false, false, false, false, false, true, false, 0);
    CHECK(strstr(s, "розширювач: недоступний") && strstr(s, "вручну") && strstr(s, "індикатор плати: червоний"));
    CHECK(strstr(s, "зумер: мовчить") && strstr(s, "0 мс"));
    console_fmt_ui_status(s, sizeof s, false, true, true, true, true, true, true, 4294967295u);
    CHECK(strstr(s, "4294967295 мс") != NULL && strlen(s) < 300);  // найдовший варіант вміщається без обрізання
    console_fmt_ui_status(s, 6, true, true, true, true, true, true, true, 5);  // малий буфер не падає
    CHECK(strlen(s) <= 5);
}

// ---------- phase4-encoders T4: команда enc (contracts/cli.md; spec AC-07, AC-09) ----------
static void test_enc_command(void)
{
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        app_state_t a = (app_state_t)st;
        CHECK(console_parse("enc", a) == CONSOLE_CMD_ENC);
        CHECK(console_parse("enc   ", a) == CONSOLE_CMD_ENC);
        CHECK(console_parse("enc reset", a) == CONSOLE_CMD_ENC_RESET);
        CHECK(console_parse("  enc   reset  ", a) == CONSOLE_CMD_ENC_RESET);
        CHECK(console_parse("enc foo", a) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_parse("enc Reset", a) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_parse("enc RESET", a) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_parse("enc reset x", a) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_parse("enc resetx", a) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_parse("enc rese", a) == CONSOLE_CMD_BAD_ARGS);
    }
    CHECK(console_identify("enc foo") == CONSOLE_CMD_ENC);
    CHECK(console_parse("encx", APP_STATE_DISARMED) == CONSOLE_CMD_UNKNOWN);
    CHECK(console_parse("Enc", APP_STATE_DISARMED) == CONSOLE_CMD_UNKNOWN);
    CHECK(strstr(console_help_text(), "enc [reset]") != NULL);
}

// Review F2: відмова enc називає, що саме не так (contracts/cli.md: невідоме друге слово, помилка в reset, третє слово).
static void test_enc_error_reason(void)
{
    CHECK(console_parse_enc("enc") == CONSOLE_ENC_SNAPSHOT);
    CHECK(console_parse_enc("  enc  ") == CONSOLE_ENC_SNAPSHOT);
    CHECK(console_parse_enc("enc reset") == CONSOLE_ENC_RESET);
    CHECK(console_parse_enc("enc foo") == CONSOLE_ENC_UNKNOWN_WORD);
    CHECK(console_parse_enc("enc re") == CONSOLE_ENC_UNKNOWN_WORD);
    CHECK(console_parse_enc("enc Reset") == CONSOLE_ENC_RESET_TYPO);
    CHECK(console_parse_enc("enc RESET") == CONSOLE_ENC_RESET_TYPO);
    CHECK(console_parse_enc("enc rese") == CONSOLE_ENC_RESET_TYPO);
    CHECK(console_parse_enc("enc resetx") == CONSOLE_ENC_RESET_TYPO);
    CHECK(console_parse_enc("enc reset x") == CONSOLE_ENC_EXTRA_WORD);
    CHECK(console_parse_enc("enc foo bar") == CONSOLE_ENC_EXTRA_WORD);

    const console_enc_result_t bad[] = { CONSOLE_ENC_UNKNOWN_WORD, CONSOLE_ENC_RESET_TYPO, CONSOLE_ENC_EXTRA_WORD };
    const char *hint[] = { "невідоме", "reset", "зайв" };
    for (int i = 0; i < 3; i++) {
        const char *m = console_msg_enc_error(bad[i]);
        CHECK(strstr(m, hint[i]) != NULL);
        CHECK(strstr(m, "«enc»") != NULL && strstr(m, "«enc reset»") != NULL);   // обидві допустимі форми
        CHECK(strstr(m, "знімок") != NULL && strstr(m, "обнул") != NULL);
        for (int j = 0; j < i; j++) CHECK(strcmp(m, console_msg_enc_error(bad[j])) != 0);
    }
    CHECK(strstr(console_msg_enc_error(CONSOLE_ENC_RESET_TYPO), "регістр") != NULL);
}

// ---------- Фаза 5: команда pid (spec AC-03, AC-04, AC-09, AC-15; матриця дозволів) ----------
static void test_pid_forms(void)
{
    console_pid_t p = console_parse_pid("pid");
    CHECK(p.form == CONSOLE_PID_SHOW);
    p = console_parse_pid("  pid   kp 1.2 ");
    CHECK(p.form == CONSOLE_PID_SET && p.param == PID_PARAM_KP && p.value == 1.2f);
    p = console_parse_pid("pid ramp 300");
    CHECK(p.form == CONSOLE_PID_SET && p.param == PID_PARAM_RAMP && p.value == 300.0f);
    p = console_parse_pid("pid kd 0");
    CHECK(p.form == CONSOLE_PID_SET && p.param == PID_PARAM_KD && p.value == 0.0f);
    CHECK(console_parse_pid("pid on").form == CONSOLE_PID_ON);
    CHECK(console_parse_pid("pid off").form == CONSOLE_PID_OFF);
    CHECK(console_parse_pid("pid save").form == CONSOLE_PID_SAVE);
    CHECK(console_parse_pid("pid trace on").form == CONSOLE_PID_TRACE_ON);
    CHECK(console_parse_pid("pid trace off").form == CONSOLE_PID_TRACE_OFF);
}

static void test_pid_errors(void)
{
    // AC-04: відʼємне, нечислове, нескінченне, кома, невідома назва, нульовий розгін, без значення, зайві слова.
    console_pid_t p = console_parse_pid("pid kp -1");
    CHECK(p.form == CONSOLE_PID_ERR_VALUE && p.why != NULL);
    CHECK(console_parse_pid("pid kp abc").form == CONSOLE_PID_ERR_NUMBER);
    CHECK(console_parse_pid("pid kp nan").form == CONSOLE_PID_ERR_NUMBER);
    CHECK(console_parse_pid("pid kp inf").form == CONSOLE_PID_ERR_NUMBER);
    CHECK(console_parse_pid("pid kp 1e3").form == CONSOLE_PID_ERR_NUMBER);
    CHECK(console_parse_pid("pid kp 1,2").form == CONSOLE_PID_ERR_NUMBER);
    CHECK(console_parse_pid("pid foo 1").form == CONSOLE_PID_ERR_WORD);
    CHECK(console_parse_pid("pid ramp 0").form == CONSOLE_PID_ERR_VALUE);
    CHECK(console_parse_pid("pid kp").form == CONSOLE_PID_ERR_MISSING);
    CHECK(console_parse_pid("pid trace").form == CONSOLE_PID_ERR_MISSING);
    CHECK(console_parse_pid("pid trace maybe").form == CONSOLE_PID_ERR_WORD);
    CHECK(console_parse_pid("pid kp 1 2").form == CONSOLE_PID_ERR_EXTRA);
    CHECK(console_parse_pid("pid on now").form == CONSOLE_PID_ERR_EXTRA);
    CHECK(console_parse_pid("pid save x").form == CONSOLE_PID_ERR_EXTRA);
    CHECK(console_parse_pid("pid trace on x").form == CONSOLE_PID_ERR_EXTRA);
    CHECK(console_parse_pid("pid KP 1").form == CONSOLE_PID_ERR_WORD);
    // AC-09: будь-яка спроба змінити межу — окрема відмова.
    CHECK(console_parse_pid("pid limit 1.0").form == CONSOLE_PID_ERR_LIMIT);
    CHECK(console_parse_pid("pid pwm 0.9").form == CONSOLE_PID_ERR_LIMIT);
    CHECK(console_parse_pid("pid max").form == CONSOLE_PID_ERR_LIMIT);

    char buf[700];   // відповідь із переліком форм — до ≈ 600 байтів UTF-8
    p = console_parse_pid("pid foo 1");
    console_fmt_pid_error(buf, sizeof buf, &p);
    CHECK(strstr(buf, "pid kp") != NULL && strstr(buf, "pid save") != NULL && strstr(buf, "pid trace on") != NULL);
    CHECK(strstr(buf, "невідʼємн") != NULL && strstr(buf, "додатн") != NULL);
    p = console_parse_pid("pid kp 1,2");
    console_fmt_pid_error(buf, sizeof buf, &p);
    CHECK(strstr(buf, "крапк") != NULL);
    p = console_parse_pid("pid kp -1");
    console_fmt_pid_error(buf, sizeof buf, &p);
    CHECK(strstr(buf, "kp") != NULL && strstr(buf, p.why) != NULL);
    p = console_parse_pid("pid limit 1.0");
    console_fmt_pid_error(buf, sizeof buf, &p);
    CHECK(strstr(buf, "межа потужності") != NULL && strstr(buf, "прошивки") != NULL);

    // Через console_parse: хибна форма — BAD_ARGS у будь-якому стані (AC-04), раніше за право за станом.
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        CHECK(console_parse("pid kp -1", (app_state_t)st) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_parse("pid limit 1", (app_state_t)st) == CONSOLE_CMD_BAD_ARGS);
        CHECK(console_identify("pid on") == CONSOLE_CMD_PID);
    }
}

static void test_pid_matrix(void)
{
    // 7 форм × 7 станів (spec §5, матриця дозволів): show/set/trace — усі; on/off — DISARMED; save — не ARMED/OBSTACLE_HOLD.
    int pairs = 0;
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        app_state_t a = (app_state_t)st;
        bool disarmed = st == APP_STATE_DISARMED;
        bool moving = st == APP_STATE_ARMED || st == APP_STATE_OBSTACLE_HOLD;
        CHECK(console_parse("pid", a) == CONSOLE_CMD_PID);
        CHECK(console_parse("pid ki 0.05", a) == CONSOLE_CMD_PID_SET);
        CHECK(console_parse("pid on", a) == (disarmed ? CONSOLE_CMD_PID_MODE : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("pid off", a) == (disarmed ? CONSOLE_CMD_PID_MODE : CONSOLE_CMD_DENIED_STATE));
        CHECK(console_parse("pid save", a) == (moving ? CONSOLE_CMD_DENIED_STATE : CONSOLE_CMD_PID_SAVE));
        CHECK(console_parse("pid trace on", a) == CONSOLE_CMD_PID_TRACE);
        CHECK(console_parse("pid trace off", a) == CONSOLE_CMD_PID_TRACE);
        CHECK(console_pid_allowed(CONSOLE_PID_ON, a) == disarmed);
        CHECK(console_pid_allowed(CONSOLE_PID_SAVE, a) == !moving);
        pairs += 7;
    }
    CHECK(pairs == 49);
    printf("ПОКРИТТЯ пар форма pid × стан: %d з 49\n", pairs);

    char buf[320];
    console_fmt_pid_denied(buf, sizeof buf, CONSOLE_PID_ON, APP_STATE_ARMED);
    CHECK(strstr(buf, "DISARMED") != NULL && strstr(buf, "ARMED") != NULL);
    console_fmt_pid_denied(buf, sizeof buf, CONSOLE_PID_SAVE, APP_STATE_ARMED);
    CHECK(strstr(buf, "під час руху") != NULL);
    CHECK(strstr(console_help_text(), "pid") != NULL);
}

int main(void)
{
    test_enc_command();
    test_enc_error_reason();
    test_arm_ok_names_bench();
    test_failsafe_denied_by_cause();
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
    test_matrix_63();
    test_pid_forms();
    test_pid_errors();
    test_pid_matrix();
    test_light_grammar();
    test_beep_grammar();
    test_ui_messages();
    test_last_stop();
    test_last_stop_real_texts();
    return test_summary();
}
