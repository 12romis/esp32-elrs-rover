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

// Очікувані 10 дозволених пар (spec AC-06).
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
    {APP_STATE_OBSTACLE_HOLD, APP_STATE_FAILSAFE},
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
    CHECK(allowed == 10);
    CHECK(refused == 39);
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

static void test_no_exits_from_failsafe_and_fault(void)
{
    for (int t = 0; t < APP_STATE_COUNT; t++) {
        CHECK(!app_sm_is_allowed(APP_STATE_FAILSAFE, (app_state_t)t));
        CHECK(!app_sm_is_allowed(APP_STATE_FAULT, (app_state_t)t));
    }
    CHECK(!app_sm_is_allowed(APP_STATE_OBSTACLE_HOLD, APP_STATE_DISARMED));
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

int main(void)
{
    test_names();
    test_all_49_pairs();
    test_boot_to_armed_refused();
    test_armed_reachable_only_from_disarmed_and_hold();
    test_no_exits_from_failsafe_and_fault();
    test_boot_sequence_history();
    test_history_overflow_keeps_last_16();
    return test_summary();
}
