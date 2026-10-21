// Тести автомата станів (AC-06, AC-01): таблиця 7x7, відмова з іменами станів, історія.
#include <string.h>

#include "app_state.h"
#include "test_support.h"

static char g_log[128];
static int g_log_calls;

static void capture_log(const char *msg)
{
    g_log_calls++;
    strncpy(g_log, msg, sizeof(g_log) - 1);
    g_log[sizeof(g_log) - 1] = '\0';
}

// Очікувані 12 дозволених пар (фаза 0: 10; фаза 3: + FAILSAFE → DISARMED, ADR-0002; фаза 6: + OBSTACLE_HOLD → DISARMED, ADR-0004).
static const struct { app_state_t from, to; } k_allowed[] = {
    {APP_STATE_BOOT, APP_STATE_SELFTEST},
    {APP_STATE_SELFTEST, APP_STATE_DISARMED},
    {APP_STATE_SELFTEST, APP_STATE_FAULT},
    {APP_STATE_DISARMED, APP_STATE_ARMED},
    {APP_STATE_DISARMED, APP_STATE_FAILSAFE},
    {APP_STATE_ARMED, APP_STATE_DISARMED},
    {APP_STATE_ARMED, APP_STATE_OBSTACLE_HOLD},
    {APP_STATE_ARMED, APP_STATE_FAILSAFE},
    {APP_STATE_OBSTACLE_HOLD, APP_STATE_ARMED},
    {APP_STATE_OBSTACLE_HOLD, APP_STATE_DISARMED},
    {APP_STATE_OBSTACLE_HOLD, APP_STATE_FAILSAFE},
    {APP_STATE_FAILSAFE, APP_STATE_DISARMED},
};
#define N_ALLOWED (sizeof(k_allowed) / sizeof(k_allowed[0]))

static bool in_expected(app_state_t from, app_state_t to)
{
    for (size_t i = 0; i < N_ALLOWED; i++) {
        if (k_allowed[i].from == from && k_allowed[i].to == to) return true;
    }
    return false;
}

// Примусово виставляє стан (обхід таблиці) для перевірки переходів з довільного стану.
static void sm_at(app_sm_t *sm, app_state_t s)
{
    app_sm_init(sm, capture_log);
    sm->state = s;
}

static void test_names(void)
{
    CHECK(strcmp(app_state_name(APP_STATE_BOOT), "BOOT") == 0);
    CHECK(strcmp(app_state_name(APP_STATE_OBSTACLE_HOLD), "OBSTACLE_HOLD") == 0);
    CHECK(strcmp(app_state_name(APP_STATE_FAULT), "FAULT") == 0);
}

static void test_all_49_pairs(void)
{
    int allowed = 0, refused = 0;
    for (int f = 0; f < APP_STATE_COUNT; f++) {
        for (int t = 0; t < APP_STATE_COUNT; t++) {
            app_sm_t sm;
            sm_at(&sm, (app_state_t)f);
            // Пара FAILSAFE → DISARMED дозволена лише з причиною «втрата зв'язку» (охорона, ADR-0002).
            if (f == APP_STATE_FAILSAFE) sm.failsafe_cause = APP_FS_LINK_LOST;
            g_log_calls = 0;
            app_sm_result_t r = app_sm_request(&sm, (app_state_t)t, 1);
            bool expect = in_expected((app_state_t)f, (app_state_t)t);
            CHECK((r == APP_SM_OK) == expect);
            CHECK(app_sm_is_allowed((app_state_t)f, (app_state_t)t) == expect);
            if (expect) {
                allowed++;
                CHECK(app_sm_state(&sm) == (app_state_t)t);
                CHECK(g_log_calls == 0);
            } else {
                refused++;
                CHECK(app_sm_state(&sm) == (app_state_t)f);
                CHECK(g_log_calls == 1);
                CHECK(strstr(g_log, app_state_name((app_state_t)f)) != NULL);
                CHECK(strstr(g_log, app_state_name((app_state_t)t)) != NULL);
            }
        }
    }
    CHECK(allowed + refused == 49);
    printf("ПОКРИТТЯ пар станів автомата: %d з 49\n", allowed + refused);
    CHECK(allowed == 12);
    CHECK(refused == 37);
}

static void test_boot_to_armed_refused(void)
{
    app_sm_t sm;
    app_sm_init(&sm, capture_log);
    g_log_calls = 0;
    CHECK(app_sm_request(&sm, APP_STATE_ARMED, 5) == APP_SM_REFUSED);
    CHECK(app_sm_state(&sm) == APP_STATE_BOOT);
    CHECK(strstr(g_log, "BOOT") != NULL && strstr(g_log, "ARMED") != NULL);
    CHECK(app_sm_history_count(&sm) == 0);
}

static void test_armed_reachable_only_from_disarmed_and_hold(void)
{
    for (int f = 0; f < APP_STATE_COUNT; f++) {
        bool ok = app_sm_is_allowed((app_state_t)f, APP_STATE_ARMED);
        bool expect = (f == APP_STATE_DISARMED || f == APP_STATE_OBSTACLE_HOLD);
        CHECK(ok == expect);
    }
}

static void test_exits_from_failsafe_and_fault(void)
{
    // З FAILSAFE єдиний вихід — DISARMED (і той лише за охороною причини); з FAULT виходів немає.
    for (int t = 0; t < APP_STATE_COUNT; t++) {
        CHECK(app_sm_is_allowed(APP_STATE_FAILSAFE, (app_state_t)t) == (t == APP_STATE_DISARMED));
        CHECK(!app_sm_is_allowed(APP_STATE_FAULT, (app_state_t)t));
    }
}

static void test_obstacle_hold_entry_and_exits(void)
{
    // Вхід в OBSTACLE_HOLD лише з ARMED; виходи — ARMED, DISARMED, FAILSAFE (phase6-tof AC-13).
    for (int f = 0; f < APP_STATE_COUNT; f++) {
        CHECK(app_sm_is_allowed((app_state_t)f, APP_STATE_OBSTACLE_HOLD) == (f == APP_STATE_ARMED));
    }
    for (int t = 0; t < APP_STATE_COUNT; t++) {
        bool expect = t == APP_STATE_ARMED || t == APP_STATE_DISARMED || t == APP_STATE_FAILSAFE;
        CHECK(app_sm_is_allowed(APP_STATE_OBSTACLE_HOLD, (app_state_t)t) == expect);
    }
    app_sm_t sm;
    sm_at(&sm, APP_STATE_OBSTACLE_HOLD);
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 1) == APP_SM_OK);   // роззброєння за один перехід
    CHECK(app_sm_state(&sm) == APP_STATE_DISARMED);
}

static void test_driving_predicate(void)
{
    for (int s = 0; s < APP_STATE_COUNT; s++) {
        bool expect = s == APP_STATE_ARMED || s == APP_STATE_OBSTACLE_HOLD;
        CHECK(app_state_is_driving((app_state_t)s) == expect);
    }
    CHECK(!app_state_is_driving(APP_STATE_COUNT));   // поза діапазоном — не їде
}

static void test_boot_sequence_history(void)
{
    app_sm_t sm;
    app_sm_init(&sm, capture_log);
    CHECK(app_sm_state(&sm) == APP_STATE_BOOT);
    CHECK(app_sm_request(&sm, APP_STATE_SELFTEST, 10) == APP_SM_OK);
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 20) == APP_SM_OK);
    CHECK(app_sm_state(&sm) == APP_STATE_DISARMED);
    CHECK(app_sm_history_count(&sm) == 2);
    app_sm_entry_t e;
    CHECK(app_sm_history_get(&sm, 0, &e));
    CHECK(e.from == APP_STATE_BOOT && e.to == APP_STATE_SELFTEST && e.t_ms == 10);
    CHECK(app_sm_history_get(&sm, 1, &e));
    CHECK(e.from == APP_STATE_SELFTEST && e.to == APP_STATE_DISARMED && e.t_ms == 20);
    CHECK(!app_sm_history_get(&sm, 2, &e));
}

static void test_history_overflow_keeps_last_16(void)
{
    app_sm_t sm;
    sm_at(&sm, APP_STATE_DISARMED);
    // 20 переходів: DISARMED<->ARMED, t_ms = 1..20
    for (uint32_t i = 1; i <= 20; i++) {
        app_state_t to = (i % 2) ? APP_STATE_ARMED : APP_STATE_DISARMED;
        CHECK(app_sm_request(&sm, to, i) == APP_SM_OK);
    }
    CHECK(app_sm_history_count(&sm) == APP_SM_HISTORY_MAX);
    app_sm_entry_t e;
    CHECK(app_sm_history_get(&sm, 0, &e) && e.t_ms == 5);   // найстарший — 5-й
    CHECK(app_sm_history_get(&sm, 15, &e) && e.t_ms == 20); // найновіший — 20-й
}

// ---------- Причина FAILSAFE й охорона виходу (ADR-0002; spec AC-08, AC-09, AC-17) ----------

static void test_failsafe_cause_and_guard(void)
{
    app_sm_t sm;

    // Вхід у FAILSAFE через зв'язок: причина запам'ятовується, вихід у DISARMED дозволений.
    sm_at(&sm, APP_STATE_ARMED);
    CHECK(sm.failsafe_cause == APP_FS_NONE);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_LINK_LOST, 100) == APP_SM_OK);
    CHECK(app_sm_state(&sm) == APP_STATE_FAILSAFE);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_LINK_LOST);
    CHECK(app_sm_history_count(&sm) == 1);
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 200) == APP_SM_OK);
    CHECK(app_sm_state(&sm) == APP_STATE_DISARMED);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_NONE);          // причина скинута після виходу

    // Вихід після відсічки по напрузі відхилено, стан незмінний.
    sm_at(&sm, APP_STATE_ARMED);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_VOLTAGE, 100) == APP_SM_OK);
    g_log_calls = 0;
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 200) == APP_SM_REFUSED);
    CHECK(app_sm_state(&sm) == APP_STATE_FAILSAFE);
    CHECK(g_log_calls == 1);                                    // відмова залогована
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_VOLTAGE);

    // Відсічка під час FAILSAFE через зв'язок підвищує причину назавжди, запису в історію немає.
    sm_at(&sm, APP_STATE_ARMED);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_LINK_LOST, 100) == APP_SM_OK);
    size_t hist = app_sm_history_count(&sm);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_VOLTAGE, 150) == APP_SM_OK);
    CHECK(app_sm_state(&sm) == APP_STATE_FAILSAFE);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_VOLTAGE);
    CHECK(app_sm_history_count(&sm) == hist);
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 200) == APP_SM_REFUSED);

    // «Втрата зв'язку» не знижує «напруга».
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_LINK_LOST, 300) == APP_SM_OK);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_VOLTAGE);

    // Старий запит FAILSAFE без причини трактується як «напруга» (безпечний бік).
    sm_at(&sm, APP_STATE_DISARMED);
    CHECK(app_sm_request(&sm, APP_STATE_FAILSAFE, 10) == APP_SM_OK);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_VOLTAGE);
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 20) == APP_SM_REFUSED);

    // Вхід у FAILSAFE з недозволеного стану відхилено, причина не ставиться.
    sm_at(&sm, APP_STATE_BOOT);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_LINK_LOST, 10) == APP_SM_REFUSED);
    CHECK(app_sm_state(&sm) == APP_STATE_BOOT);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_NONE);

    // Повторний цикл: після виходу новий FAILSAFE ставить нову причину.
    sm_at(&sm, APP_STATE_ARMED);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_LINK_LOST, 1) == APP_SM_OK);
    CHECK(app_sm_request(&sm, APP_STATE_DISARMED, 2) == APP_SM_OK);
    CHECK(app_sm_request(&sm, APP_STATE_ARMED, 3) == APP_SM_OK);
    CHECK(app_sm_enter_failsafe(&sm, APP_FS_VOLTAGE, 4) == APP_SM_OK);
    CHECK(app_sm_failsafe_cause(&sm) == APP_FS_VOLTAGE);

    // Імена причин для журналу й status.
    CHECK(strcmp(app_failsafe_cause_name(APP_FS_LINK_LOST), "втрата звʼязку") == 0);
    CHECK(strcmp(app_failsafe_cause_name(APP_FS_VOLTAGE), "напруга") == 0);
    CHECK(strcmp(app_failsafe_cause_name(APP_FS_NONE), "немає") == 0);
}

int main(void)
{
    test_names();
    test_all_49_pairs();
    test_boot_to_armed_refused();
    test_armed_reachable_only_from_disarmed_and_hold();
    test_exits_from_failsafe_and_fault();
    test_obstacle_hold_entry_and_exits();
    test_driving_predicate();
    test_failsafe_cause_and_guard();
    test_boot_sequence_history();
    test_history_overflow_keeps_last_16();
    return test_summary();
}
