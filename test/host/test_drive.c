// Хостові тести drive_core (spec AC-03, AC-04, AC-10, AC-20, AC-21 — T2; решта — T3, T4).
#include <stdint.h>

#include <math.h>
#include <string.h>

#include "battery_calib.h"
#include "console_core.h"
#include "drive_core.h"
#include "test_support.h"

static void check_off(drive_outputs_t o)
{
    CHECK(o.stby == 0);
    CHECK(o.left.duty_permille == 0 && o.left.dir1 == 0 && o.left.dir2 == 0);
    CHECK(o.right.duty_permille == 0 && o.right.dir1 == 0 && o.right.dir2 == 0);
}

static void test_mix(void)
{
    float l, r;
    drive_mix(0.3f, 0.0f, &l, &r);
    CHECK(l > 0.2999f && l < 0.3001f && r > 0.2999f && r < 0.3001f);
    drive_mix(0.0f, 0.4f, &l, &r);
    CHECK(l > 0.3999f && l < 0.4001f && r < -0.3999f && r > -0.4001f);
    // 0.8 / 0.5: сума 1.3 → ділення на 1.3, співвідношення зберігається
    drive_mix(0.8f, 0.5f, &l, &r);
    CHECK(l > 0.9999f && l < 1.0001f);
    CHECK(r > 0.2307f && r < 0.2309f);
    drive_mix(-0.8f, 0.5f, &l, &r);   // −0.3 і −1.3 → ділення на 1.3
    CHECK(r < -0.9999f && l > -0.2309f && l < -0.2307f);
}

static void test_outputs_from(void)
{
    drive_outputs_t o = drive_outputs_from(0.3f, 0.3f);
    CHECK(o.stby == 1);
    CHECK(o.left.duty_permille == 300 && o.left.dir1 == 1 && o.left.dir2 == 0);
    CHECK(o.right.duty_permille == 300 && o.right.dir1 == 1 && o.right.dir2 == 0);

    // 0.8 / 0.5 → 1.0 і 0.2308 → насичення 750 і 231 проміле (AC-04)
    float l, r;
    drive_mix(0.8f, 0.5f, &l, &r);
    o = drive_outputs_from(l, r);
    CHECK(o.left.duty_permille == 750);
    CHECK(o.right.duty_permille == 231);

    // Поворот на місці: ліве вперед, праве назад (AC-04)
    drive_mix(0.0f, 0.4f, &l, &r);
    o = drive_outputs_from(l, r);
    CHECK(o.left.duty_permille == 400 && o.left.dir1 == 1 && o.left.dir2 == 0);
    CHECK(o.right.duty_permille == 400 && o.right.dir1 == 0 && o.right.dir2 == 1);
}

static void test_cap(void)
{
    CHECK(DRIVE_PWM_MAX_PERMILLE == 750);
    drive_outputs_t o = drive_outputs_from(1.0f, 1.0f);
    CHECK(o.left.duty_permille == 750 && o.right.duty_permille == 750);
    o = drive_outputs_from(-1.0f, -1.0f);
    CHECK(o.left.duty_permille == 750 && o.left.dir1 == 0 && o.left.dir2 == 1);
    CHECK(o.right.duty_permille == 750 && o.right.dir1 == 0 && o.right.dir2 == 1);
    // жодна комбінація входу в [−1, +1] не дає більше 750
    for (int i = -10; i <= 10; i++) {
        for (int j = -10; j <= 10; j++) {
            float l, r;
            drive_mix((float)i / 10.0f, (float)j / 10.0f, &l, &r);
            o = drive_outputs_from(l, r);
            CHECK(o.left.duty_permille <= 750 && o.right.duty_permille <= 750);
        }
    }
}

static void test_zero_side_and_off(void)
{
    float l, r;
    drive_mix(0.3f, 0.3f, &l, &r);          // праве = 0 (AC-21)
    drive_outputs_t o = drive_outputs_from(l, r);
    CHECK(o.stby == 1);                      // STBY піднята при нулі на одному борту
    CHECK(o.left.duty_permille == 600 && o.left.dir1 == 1 && o.left.dir2 == 0);
    CHECK(o.right.duty_permille == 0 && o.right.dir1 == 0 && o.right.dir2 == 0);

    o = drive_outputs_from(0.0f, 0.0f);      // drive 0 0 — звичайна команда: STBY вгору, ШІМ 0
    CHECK(o.stby == 1);
    CHECK(o.left.duty_permille == 0 && o.left.dir1 == 0 && o.left.dir2 == 0);
    CHECK(o.right.duty_permille == 0 && o.right.dir1 == 0 && o.right.dir2 == 0);

    check_off(drive_outputs_off());          // AC-20: вибіг — усі нулі
}

// ---------- T3: запити arm/disarm/drive, дозволи, причини відмов ----------
static battery_snapshot_t snap(uint32_t mv, uint32_t t_ms) { battery_snapshot_t b = {mv, t_ms, true}; return b; }

static drive_ctl_in_t in_for(app_state_t st, uint32_t now, battery_snapshot_t b, drive_req_kind_t k, float thr, float steer)
{
    drive_ctl_in_t in = {0};
    in.now_ms = now; in.state = st; in.battery = b;
    in.req.kind = k; in.req.throttle = thr; in.req.steer = steer;
    return in;
}

static void test_arm(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    // arm у DISARMED, напруга придатна й не нижча за поріг → прийнято, запит ARMED, виходи вимкнені (AC-01)
    drive_ctl_step(&c, &(drive_ctl_in_t){0}, &o);   // порожній вхід не падає
    drive_ctl_in_t in = in_for(APP_STATE_DISARMED, 1000, snap(6000, 1000), DRIVE_REQ_ARM, 0, 0);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.accepted && o.reason == DRIVE_REASON_NONE);
    CHECK(o.has_state_request && o.state_request == APP_STATE_ARMED);
    check_off(o.out);
    // 5999 мВ → LOW_VOLTAGE (AC-13)
    in = in_for(APP_STATE_DISARMED, 1000, snap(5999, 1000), DRIVE_REQ_ARM, 0, 0);
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_LOW_VOLTAGE && !o.has_state_request && o.mv == 5999);
    // немає вимірювання (невалідний) і застарілий (601 мс) → NO_MEASUREMENT
    battery_snapshot_t none = {0, 0, false};
    in = in_for(APP_STATE_DISARMED, 1000, none, DRIVE_REQ_ARM, 0, 0);
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_NO_MEASUREMENT && !o.has_state_request);
    in = in_for(APP_STATE_DISARMED, 1601, snap(8000, 1000), DRIVE_REQ_ARM, 0, 0);
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_NO_MEASUREMENT);
    // arm не в DISARMED (AC-07)
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        if (st == APP_STATE_DISARMED) continue;
        drive_ctl_init(&c);
        in = in_for((app_state_t)st, 1000, snap(8000, 1000), DRIVE_REQ_ARM, 0, 0);
        drive_ctl_step(&c, &in, &o);
        CHECK(!o.accepted && o.reason == DRIVE_REASON_NOT_DISARMED);
        // у OBSTACLE_HOLD запит ARMED буде лише як звільнення сектору (phase6-tof), а не озброєння
        if (st != APP_STATE_OBSTACLE_HOLD) CHECK(!(o.has_state_request && o.state_request == APP_STATE_ARMED));
    }
}

static void test_drive_req(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // drive поза рухом (AC-06): відхилено, виходи вимкнені; в ARMED і OBSTACLE_HOLD приймається (phase6-tof AC-15)
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        if (st == APP_STATE_ARMED || st == APP_STATE_OBSTACLE_HOLD) continue;
        drive_ctl_init(&c);
        drive_ctl_in_t in = in_for((app_state_t)st, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
        drive_ctl_step(&c, &in, &o);
        CHECK(!o.accepted && o.reason == DRIVE_REASON_NOT_ARMED);
        check_off(o.out);
    }
    // drive у ARMED: прийнято, STBY піднята, значення бортів 300/300 (AC-03)
    drive_ctl_init(&c);
    drive_ctl_in_t in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.accepted && o.out.stby == 1);
    CHECK(o.left_permille == 300 && o.right_permille == 300);
    CHECK(!o.has_state_request);
    // поза діапазоном, NaN, inf → відхилено, виходи не змінюються (захист після розбору в консолі)
    drive_ctl_init(&c);
    in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 1.5f, 0.0f);
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_OUT_OF_RANGE); check_off(o.out);
    in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, NAN, 0.0f);
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_OUT_OF_RANGE);
    in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.0f, INFINITY);
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_OUT_OF_RANGE);
    // межі ±1 входять у діапазон
    in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 1.0f, -1.0f);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.accepted);
    // drive 0 0 — звичайна команда: STBY вгору (AC-21)
    drive_ctl_init(&c);
    in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.0f, 0.0f);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.accepted && o.out.stby == 1 && o.left_permille == 0 && o.right_permille == 0);
}

static void test_disarm_req(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        drive_ctl_init(&c);
        drive_ctl_in_t in = in_for((app_state_t)st, 1000, snap(8000, 1000), DRIVE_REQ_DISARM, 0, 0);
        drive_ctl_step(&c, &in, &o);
        CHECK(o.accepted);               // disarm приймається в кожному стані (AC-09)
        check_off(o.out);
        if (st == APP_STATE_ARMED) {
            CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
        } else if (st != APP_STATE_OBSTACLE_HOLD) {   // OBSTACLE_HOLD — окремо в test_hold_*
            CHECK(!o.has_state_request);
        }
    }
    // disarm під час руху: виходи вимкнено (AC-02), запит DISARMED
    drive_ctl_init(&c);
    drive_ctl_in_t in = in_for(APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.out.stby == 1);
    in = in_for(APP_STATE_ARMED, 1020, snap(8000, 1000), DRIVE_REQ_DISARM, 0, 0);
    drive_ctl_step(&c, &in, &o);
    check_off(o.out);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
}

static void test_ordering(void)
{
    // Запит стану ніколи не з'являється при ввімкнених виходах (виходи спершу, ADR-0002)
    drive_ctl_t c; drive_ctl_out_t o;
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        for (int k = 0; k <= DRIVE_REQ_DRIVE; k++) {
            drive_ctl_init(&c);
            drive_ctl_in_t in = in_for((app_state_t)st, 1000, snap(8000, 1000), (drive_req_kind_t)k, 0.5f, 0.0f);
            drive_ctl_step(&c, &in, &o);
            // ADR-0002 фази 1: запити, що зупиняють мотори (DISARMED, FAILSAFE), — з уже вимкненими виходами;
            // запити OBSTACLE_HOLD ↔ ARMED виходів не вимикають (phase6-tof ADR-0001)
            if (o.has_state_request && (o.state_request == APP_STATE_DISARMED || o.state_request == APP_STATE_FAILSAFE)) CHECK(o.out.stby == 0);
            if (!app_state_is_driving((app_state_t)st)) check_off(o.out);   // AC-08: поза рухом виходи вимкнені
        }
    }
}

// ---------- T4: вікно команди 2 с, відсічка по напрузі, порядок зупинки ----------
static drive_ctl_out_t go(drive_ctl_t *c, app_state_t st, uint32_t now, battery_snapshot_t b,
                          drive_req_kind_t k, float thr, float steer)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(st, now, b, k, thr, steer);
    drive_ctl_step(c, &in, &o);
    return o;
}

static void test_window(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    battery_snapshot_t b = snap(8000, 1000);
    o = go(&c, APP_STATE_ARMED, 1000, b, DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    CHECK(o.accepted && o.out.stby == 1 && o.remaining_ms == 2000);
    b = snap(8000, 2900);
    o = go(&c, APP_STATE_ARMED, 2999, b, DRIVE_REQ_NONE, 0, 0);       // 1999 мс: ще їде
    CHECK(o.out.stby == 1 && !o.has_state_request && o.stop == DRIVE_STOP_NONE && o.remaining_ms == 1);
    b = snap(8000, 2990);
    o = go(&c, APP_STATE_ARMED, 3000, b, DRIVE_REQ_NONE, 0, 0);       // 2000 мс: спливло (AC-11)
    check_off(o.out);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
    CHECK(o.stop == DRIVE_STOP_WINDOW_EXPIRED && o.left_permille == 0 && o.right_permille == 0);
    // після спливання стан став DISARMED (HAL застосував): виходи вимкнені, запитів немає
    o = go(&c, APP_STATE_DISARMED, 3020, b, DRIVE_REQ_NONE, 0, 0);
    check_off(o.out); CHECK(!o.has_state_request && o.stop == DRIVE_STOP_NONE);
}

static void test_window_repeat_and_reject(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    battery_snapshot_t b = snap(8000, 1000);
    go(&c, APP_STATE_ARMED, 1000, b, DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    // повтор на 1999 мс перезапускає відлік (AC-11)
    o = go(&c, APP_STATE_ARMED, 2999, snap(8000, 2900), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    CHECK(o.accepted && o.remaining_ms == 2000);
    o = go(&c, APP_STATE_ARMED, 4900, snap(8000, 4800), DRIVE_REQ_NONE, 0, 0);   // 1901 мс від повтору
    CHECK(o.out.stby == 1 && !o.has_state_request);
    // відхилена команда вікно не продовжує
    o = go(&c, APP_STATE_ARMED, 4950, snap(8000, 4800), DRIVE_REQ_DRIVE, 5.0f, 0.0f);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_OUT_OF_RANGE);
    CHECK(o.out.stby == 1);                                   // попередня команда діє
    o = go(&c, APP_STATE_ARMED, 4999, snap(8000, 4900), DRIVE_REQ_NONE, 0, 0);   // 2000 від 2999
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
    check_off(o.out);
}

static void test_cutoff(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // відсічка в русі (AC-14)
    drive_ctl_init(&c);
    go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    o = go(&c, APP_STATE_ARMED, 1100, snap(5999, 1100), DRIVE_REQ_NONE, 0, 0);
    check_off(o.out);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE);
    CHECK(o.stop == DRIVE_STOP_LOW_VOLTAGE && o.mv == 5999);
    // ARMED без руху, напруга впала → також FAILSAFE
    drive_ctl_init(&c);
    o = go(&c, APP_STATE_ARMED, 1000, snap(5000, 1000), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE && o.stop == DRIVE_STOP_LOW_VOLTAGE);
    // знімок застарів (601 мс) або невалідний у ARMED → FAILSAFE
    drive_ctl_init(&c);
    o = go(&c, APP_STATE_ARMED, 1601, snap(8000, 1000), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE && o.stop == DRIVE_STOP_STALE_VOLTAGE);
    battery_snapshot_t none = {0, 0, false};
    o = go(&c, APP_STATE_ARMED, 1000, none, DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE);
    // рівно 6000 мВ і свіжий знімок 600 мс → не відсікає
    drive_ctl_init(&c);
    o = go(&c, APP_STATE_ARMED, 1600, snap(6000, 1000), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request);
    // drive у тому ж такті, коли напруга впала: відсічка має перевагу, команда не виконується
    drive_ctl_init(&c);
    o = go(&c, APP_STATE_ARMED, 1000, snap(5900, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    CHECK(!o.accepted && o.has_state_request && o.state_request == APP_STATE_FAILSAFE);
    check_off(o.out);
    // низька напруга в DISARMED стан не змінює (AC-22)
    drive_ctl_init(&c);
    o = go(&c, APP_STATE_DISARMED, 1000, snap(5000, 1000), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request); check_off(o.out);
    // знову можливий arm, щойно напруга відновилась, без скидання
    o = go(&c, APP_STATE_DISARMED, 1200, snap(7500, 1200), DRIVE_REQ_ARM, 0, 0);
    CHECK(o.accepted && o.state_request == APP_STATE_ARMED);
}

static void test_min_mv_and_wrap(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    go(&c, APP_STATE_ARMED, 1100, snap(7200, 1100), DRIVE_REQ_NONE, 0, 0);
    go(&c, APP_STATE_ARMED, 1200, snap(7600, 1200), DRIVE_REQ_NONE, 0, 0);
    o = go(&c, APP_STATE_ARMED, 3000, snap(7900, 2900), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.stop == DRIVE_STOP_WINDOW_EXPIRED && o.min_mv == 7200);
    // перехід лічильника мс через нуль
    drive_ctl_init(&c);
    uint32_t t0 = 0xFFFFFF00u;
    go(&c, APP_STATE_ARMED, t0, snap(8000, t0), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    o = go(&c, APP_STATE_ARMED, t0 + 1999u, snap(8000, t0 + 1900u), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && o.out.stby == 1);
    o = go(&c, APP_STATE_ARMED, t0 + 2000u, snap(8000, t0 + 1900u), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
}

static void test_all_states_off(void)
{
    // AC-08/AC-20: поза ARMED виходи вимкнені, навіть якщо раніше було вікно
    drive_ctl_t c; drive_ctl_out_t o;
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        if (st == APP_STATE_ARMED || st == APP_STATE_OBSTACLE_HOLD) continue;   // OBSTACLE_HOLD — стан руху (phase6-tof)
        drive_ctl_init(&c);
        go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
        o = go(&c, (app_state_t)st, 1020, snap(8000, 1000), DRIVE_REQ_NONE, 0, 0);
        check_off(o.out);
    }
}

static void test_reason_text(void)
{
    char b[96];
    drive_reason_text(DRIVE_REASON_LOW_VOLTAGE, 5800, b, sizeof b);
    CHECK(strstr(b, "5800") && strstr(b, "6000"));
    drive_reason_text(DRIVE_REASON_NO_MEASUREMENT, 0, b, sizeof b);
    CHECK(strstr(b, "вимірювання") != NULL);
    drive_reason_text(DRIVE_REASON_NOT_ARMED, 0, b, sizeof b);
    CHECK(strstr(b, "ARMED") != NULL);
    drive_reason_text(DRIVE_REASON_NOT_DISARMED, 0, b, 4);   // малий буфер не падає
    CHECK(strlen(b) <= 3);
}

static void test_stop_text_and_disarm_min(void)
{
    CHECK(strstr(drive_stop_text(DRIVE_STOP_WINDOW_EXPIRED), "спливла") != NULL);
    CHECK(strstr(drive_stop_text(DRIVE_STOP_LOW_VOLTAGE), "нижча за поріг") != NULL);
    CHECK(strstr(drive_stop_text(DRIVE_STOP_STALE_VOLTAGE), "вимірювання") != NULL);
    CHECK(strstr(drive_stop_text(DRIVE_STOP_DISARM), "disarm") != NULL);
    CHECK(strlen(drive_stop_text(DRIVE_STOP_NONE)) > 0);
    // disarm під час руху повертає мінімальну напругу за вікно
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    go(&c, APP_STATE_ARMED, 1100, snap(7300, 1100), DRIVE_REQ_NONE, 0, 0);
    o = go(&c, APP_STATE_ARMED, 1200, snap(7700, 1200), DRIVE_REQ_DISARM, 0, 0);
    CHECK(o.stop == DRIVE_STOP_DISARM && o.min_mv == 7300);
    // відсічка під час руху також повертає мінімум
    drive_ctl_init(&c);
    go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    o = go(&c, APP_STATE_ARMED, 1100, snap(5900, 1100), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.stop == DRIVE_STOP_LOW_VOLTAGE && o.min_mv == 5900 && o.mv == 5900);
}

static void test_future_snapshot_and_texts(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // знімок із t_ms > now не дає хибної відсічки в ARMED
    drive_ctl_init(&c);
    o = go(&c, APP_STATE_ARMED, 1000, snap(8000, 1001), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && o.stop == DRIVE_STOP_NONE);
    // арм зі знімком із майбутнього дозволений
    o = go(&c, APP_STATE_DISARMED, 1000, snap(8000, 1003), DRIVE_REQ_ARM, 0, 0);
    CHECK(o.accepted);
    // текст відповіді на disarm (AC-09, phase3-crsf AC-08, AC-09, AC-16)
    char b[192];
    drive_disarm_text(APP_STATE_ARMED, true, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_NONE, DRIVE_FSX_RESET_ONLY, false, b, sizeof b);
    CHECK(strstr(b, "роззброєно") != NULL && strstr(b, "тумблер") == NULL);
    drive_disarm_text(APP_STATE_ARMED, true, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_NONE, DRIVE_FSX_RESET_ONLY, true, b, sizeof b);
    CHECK(strstr(b, "роззброєно") != NULL && strstr(b, "DISARM") != NULL && strstr(b, "знову") != NULL);   // AC-16
    drive_disarm_text(APP_STATE_DISARMED, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_NONE, DRIVE_FSX_RESET_ONLY, false, b, sizeof b);
    CHECK(strstr(b, "DISARMED") != NULL && strstr(b, "лише з ARMED") != NULL);
    // FAILSAFE: пояснення за причиною й тим, чого бракує (AC-08, AC-09).
    drive_disarm_text(APP_STATE_FAILSAFE, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_VOLTAGE, DRIVE_FSX_RESET_ONLY, false, b, sizeof b);
    CHECK(strstr(b, "FAILSAFE") != NULL && strstr(b, "скиданням") != NULL);
    drive_disarm_text(APP_STATE_FAILSAFE, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_LINK_LOST, DRIVE_FSX_SWITCH_ARM, false, b, sizeof b);
    CHECK(strstr(b, "FAILSAFE") != NULL && strstr(b, "DISARM") != NULL);
    drive_disarm_text(APP_STATE_FAILSAFE, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_LINK_LOST, DRIVE_FSX_STABILIZING, false, b, sizeof b);
    CHECK(strstr(b, "стабільн") != NULL);
    drive_disarm_text(APP_STATE_ARMED, true, APP_STATE_FAILSAFE, DRIVE_STOP_LOW_VOLTAGE, APP_FS_VOLTAGE, DRIVE_FSX_RESET_ONLY, false, b, sizeof b);
    CHECK(strstr(b, "відсічка") != NULL && strstr(b, "FAILSAFE") != NULL);
    drive_disarm_text(APP_STATE_ARMED, true, APP_STATE_FAILSAFE, DRIVE_STOP_LINK_LOST, APP_FS_LINK_LOST, DRIVE_FSX_NO_LINK, false, b, sizeof b);
    CHECK(strstr(b, "звʼязку") != NULL && strstr(b, "FAILSAFE") != NULL);
    // Усі відповіді вміщуються у буфер відповіді консолі (192 байти) без обрізання.
    for (int f = 0; f <= DRIVE_FSX_SWITCH_ARM; f++) {
        drive_disarm_text(APP_STATE_FAILSAFE, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, APP_FS_LINK_LOST, (drive_fsx_t)f, false, b, sizeof b);
        CHECK(strlen(b) < sizeof b - 1);
    }
    drive_reason_text(DRIVE_REASON_TRANSITION_REFUSED, 0, b, sizeof b);
    CHECK(strstr(b, "перехід") != NULL);
}

// ---------- phase3-crsf T4: стендовий режим, таймер звʼязку, відмова «керує пульт» ----------

// Знімок приймача: кадр прийнято в момент t_ms, стіки в спокої, тумблер у ARM (ARMED-сценарії T4 не мають
// одразу роззброюватися тумблером; для DISARM-сценаріїв T5 є rc_raw).
static crsf_snapshot_t rc_snap(uint32_t t_ms, uint32_t seq)
{
    crsf_snapshot_t r;
    memset(&r, 0, sizeof(r));
    r.have_frame = true;
    r.seq = seq;
    r.t_ms = t_ms;
    for (int i = 0; i < CRSF_CHANNELS; i++) r.ch[i] = CRSF_RAW_CENTER;
    r.ch[CRSF_IDX_ARM] = 1792;
    return r;
}

static drive_ctl_out_t go_rc(drive_ctl_t *c, app_state_t st, uint32_t now, battery_snapshot_t b, crsf_snapshot_t rc,
                             drive_req_kind_t k, float thr, float steer)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(st, now, b, k, thr, steer);
    in.rc = rc;
    drive_ctl_step(c, &in, &o);
    return o;
}

static void test_bench_without_frames_is_phase1(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    CHECK(!c.remote_active);
    // Без кадрів керування як у фазі 1: arm і drive приймаються з консолі (AC-13).
    o = go(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), DRIVE_REQ_ARM, 0, 0);
    CHECK(o.accepted && o.state_request == APP_STATE_ARMED);
    o = go(&c, APP_STATE_ARMED, 1020, snap(8000, 1020), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    CHECK(o.accepted && o.out.stby == 1 && o.left_permille == 300);
    // Спливання вікна 2 с — як у фазі 1.
    o = go(&c, APP_STATE_ARMED, 3020, snap(8000, 3000), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.stop == DRIVE_STOP_WINDOW_EXPIRED && o.state_request == APP_STATE_DISARMED);
    CHECK(!c.remote_active && o.events == 0);
}

static void test_bench_ends_on_first_frame(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);   // консоль їде
    // Перший кадр пульта: мотори зупинено, DISARMED, стенд закінчено назавжди (AC-14).
    o = go_rc(&c, APP_STATE_ARMED, 1100, snap(8000, 1100), rc_snap(1100, 1), DRIVE_REQ_NONE, 0, 0);
    check_off(o.out);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
    CHECK(o.stop == DRIVE_STOP_REMOTE_APPEARED);
    CHECK(o.events & DRIVE_EVT_REMOTE_APPEARED);
    CHECK(!(o.events & DRIVE_EVT_LINK_RESTORED));      // перша поява — не «відновлення»
    CHECK(c.remote_active && c.link_alive);
    // Стенд не повертається, навіть коли знімок порожній (приймач вимкнено): remote_active лишається.
    o = go(&c, APP_STATE_DISARMED, 3000, snap(8000, 3000), DRIVE_REQ_NONE, 0, 0);
    CHECK(c.remote_active);
    // ARMED без руху з консолі, перший кадр: теж роззброює.
    drive_ctl_init(&c);
    o = go_rc(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED && o.stop == DRIVE_STOP_REMOTE_APPEARED);
    // DISARMED: стенд закінчується без переходу.
    drive_ctl_init(&c);
    o = go_rc(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && c.remote_active && (o.events & DRIVE_EVT_REMOTE_APPEARED));
    // FAILSAFE у стенді (відсічка), перший кадр: стенд закінчується, стан без змін.
    drive_ctl_init(&c);
    o = go_rc(&c, APP_STATE_FAILSAFE, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(c.remote_active && !o.has_state_request);
}

static void test_console_refused_after_bench(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    drive_ctl_init(&c);
    // Стенд закінчився (є кадр), звʼязок живий.
    go_rc(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_DISARMED, 1020, snap(8000, 1020), rc_snap(1010, 2), DRIVE_REQ_ARM, 0, 0);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_REMOTE_ONLY && !o.has_state_request);
    check_off(o.out);
    // Стенд закінчено, звʼязок втрачено (>500 мс): консоль так само відхилена (AC-07, AC-15).
    o = go_rc(&c, APP_STATE_DISARMED, 2000, snap(8000, 2000), rc_snap(1010, 2), DRIVE_REQ_ARM, 0, 0);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_REMOTE_ONLY);
    // drive у ARMED у режимі «пульт»: клас 3, а не виконання.
    drive_ctl_init(&c);
    go_rc(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_ARMED, 1020, snap(8000, 1020), rc_snap(1010, 2), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_REMOTE_ONLY);
    CHECK(o.left_permille == 0 && o.right_permille == 0);
    // Порядок: спершу стан (клас 1), потім «керує пульт» (клас 3).
    o = go_rc(&c, APP_STATE_ARMED, 1040, snap(8000, 1040), rc_snap(1030, 3), DRIVE_REQ_ARM, 0, 0);
    CHECK(o.reason == DRIVE_REASON_NOT_DISARMED);
    o = go_rc(&c, APP_STATE_DISARMED, 1060, snap(8000, 1060), rc_snap(1050, 4), DRIVE_REQ_DRIVE, 0.1f, 0.0f);
    CHECK(o.reason == DRIVE_REASON_NOT_ARMED);
    // disarm працює завжди (AC-15): ARMED → DISARMED.
    o = go_rc(&c, APP_STATE_ARMED, 1080, snap(8000, 1080), rc_snap(1070, 5), DRIVE_REQ_DISARM, 0, 0);
    CHECK(o.accepted && o.state_request == APP_STATE_DISARMED && o.stop == DRIVE_STOP_DISARM);
    // Текст відмови називає пульт і перезапуск.
    char b[96];                                           // як console_motor_reply_t.text
    drive_reason_text(DRIVE_REASON_REMOTE_ONLY, 0, b, sizeof b);
    CHECK(strstr(b, "пульт") != NULL && strstr(b, "перезапуск") != NULL);
    CHECK(strlen(b) < sizeof b - 1);                      // не обрізано
}

static void test_link_timer(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    battery_snapshot_t b = snap(8000, 0);
    // ARMED з пульта (звʼязок є), потім кадри припиняються.
    drive_ctl_init(&c);
    go_rc(&c, APP_STATE_ARMED, 1000, b, rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    b = snap(8000, 1400);
    o = go_rc(&c, APP_STATE_ARMED, 1500, b, rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);     // рівно 500 мс — ще живий
    CHECK(c.link_alive && !o.has_state_request);
    b = snap(8000, 1500);
    o = go_rc(&c, APP_STATE_ARMED, 1501, b, rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);     // 501 мс — втрата
    CHECK(!c.link_alive);
    check_off(o.out);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE);
    CHECK(o.failsafe_cause == APP_FS_LINK_LOST);
    CHECK(o.stop == DRIVE_STOP_LINK_LOST);
    CHECK(o.events & DRIVE_EVT_LINK_LOST);
    CHECK(c.sw_base == CRSF_SW_UNKNOWN);                                                 // база тумблера скинута
    // Перехід у FAILSAFE один: наступний такт у FAILSAFE нових подій втрати не дає.
    o = go_rc(&c, APP_STATE_FAILSAFE, 1521, b, rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(!(o.events & DRIVE_EVT_LINK_LOST));
    check_off(o.out);

    // Кадри не оновлюються (мітка стара), хоч лічильники службових кадрів ростуть: знімок з тією ж міткою й seq.
    drive_ctl_init(&c);
    crsf_snapshot_t r = rc_snap(1000, 1);
    go_rc(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), r, DRIVE_REQ_NONE, 0, 0);
    r.cnt.other = 300;                                                                   // приймач шле статистику
    o = go_rc(&c, APP_STATE_ARMED, 1600, snap(8000, 1600), r, DRIVE_REQ_NONE, 0, 0);
    CHECK(o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_LINK_LOST);

    // Втрата в DISARMED: без переходу, лише подія, база скинута (AC-07).
    drive_ctl_init(&c);
    go_rc(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_DISARMED, 1600, snap(8000, 1600), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && (o.events & DRIVE_EVT_LINK_LOST) && !c.link_alive);
    check_off(o.out);
    // Відновлення: подія один раз, початок стабільного звʼязку = мітка першого кадру.
    o = go_rc(&c, APP_STATE_DISARMED, 3000, snap(8000, 3000), rc_snap(2990, 40), DRIVE_REQ_NONE, 0, 0);
    CHECK((o.events & DRIVE_EVT_LINK_RESTORED) && c.link_alive && c.link_since_ms == 2990);
    o = go_rc(&c, APP_STATE_DISARMED, 3020, snap(8000, 3020), rc_snap(3010, 42), DRIVE_REQ_NONE, 0, 0);
    CHECK(!(o.events & DRIVE_EVT_LINK_RESTORED) && c.link_since_ms == 2990);

    // Відсічка по напрузі в ARMED тепер несе причину «напруга» (ADR-0002).
    drive_ctl_init(&c);
    o = go_rc(&c, APP_STATE_ARMED, 1000, snap(5900, 1000), rc_snap(990, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_VOLTAGE && o.stop == DRIVE_STOP_LOW_VOLTAGE);
    // Одночасно втрата звʼязку й низька напруга: відсічка має перевагу.
    drive_ctl_init(&c);
    go_rc(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_ARMED, 1700, snap(5900, 1700), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.failsafe_cause == APP_FS_VOLTAGE && o.stop == DRIVE_STOP_LOW_VOLTAGE);
    // Старий стенд: відсічка в русі без кадрів теж «напруга».
    drive_ctl_init(&c);
    go(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), DRIVE_REQ_DRIVE, 0.3f, 0.0f);
    o = go(&c, APP_STATE_ARMED, 1100, snap(5999, 1100), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.failsafe_cause == APP_FS_VOLTAGE);
}

static void test_link_stop_texts(void)
{
    CHECK(strstr(drive_stop_text(DRIVE_STOP_LINK_LOST), "звʼязку") != NULL);
    CHECK(strstr(drive_stop_text(DRIVE_STOP_REMOTE_APPEARED), "пульт") != NULL);
    CHECK(strstr(drive_stop_text(DRIVE_STOP_SWITCH_DISARM), "тумблер") != NULL);
}

// ---------- phase3-crsf T5: озброєння фронтом тумблера, рух стіком, роззброєння ----------

static crsf_snapshot_t rc_raw(uint32_t t_ms, uint32_t seq, uint16_t steer, uint16_t thr, uint16_t sw)
{
    crsf_snapshot_t r = rc_snap(t_ms, seq);
    r.ch[CRSF_IDX_STEER] = steer;
    r.ch[CRSF_IDX_THROTTLE] = thr;
    r.ch[CRSF_IDX_ARM] = sw;
    return r;
}

#define SW_DIS 191
#define SW_ARM 1792
#define NEUT 992

// Готує ровер у DISARMED зі звʼязком і тумблером у DISARM (база встановлена), повертає наступну мітку часу.
static uint32_t ready_disarmed(drive_ctl_t *c, uint32_t t0)
{
    drive_ctl_init(c);
    go_rc(c, APP_STATE_DISARMED, t0, snap(8000, t0), rc_raw(t0, 1, NEUT, NEUT, SW_DIS), DRIVE_REQ_NONE, 0, 0);
    go_rc(c, APP_STATE_DISARMED, t0 + 20, snap(8000, t0 + 20), rc_raw(t0 + 20, 2, NEUT, NEUT, SW_DIS), DRIVE_REQ_NONE, 0, 0);
    return t0 + 40;
}

static void test_remote_arm_edge(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    uint32_t t = ready_disarmed(&c, 1000);
    CHECK(c.sw_base == CRSF_SW_DISARM);
    // Фронт DISARM → ARM за всіх умов: запит ARMED, STBY ще нижче (виходи вимикає відсутність руху).
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_ARMED);
    CHECK(o.events & DRIVE_EVT_REMOTE_ARMED);
    check_off(o.out);
    // Тумблер лишається в ARM, ровер вже ARMED: другого фронту немає.
    o = go_rc(&c, APP_STATE_ARMED, t + 20, snap(8000, t + 20), rc_raw(t + 20, 4, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && !(o.events & DRIVE_EVT_REMOTE_ARMED));
    // Дрібний стік у мертвій зоні (спокій зміщений, як виміряно) не заважає озброєнню.
    t = ready_disarmed(&c, 5000);
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, 982, 978, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.state_request == APP_STATE_ARMED && o.has_state_request);
}

static void test_remote_arm_refusals(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    uint32_t t;
    // Стік не в нейтралі.
    t = ready_disarmed(&c, 1000);
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, 1500, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && (o.events & DRIVE_EVT_ARM_REFUSED) && o.arm_block == DRIVE_BLOCK_NOT_NEUTRAL);
    // Повернення стіка в нейтраль при тумблері в ARM ровер не озброює (AC-04).
    o = go_rc(&c, APP_STATE_DISARMED, t + 20, snap(8000, t + 20), rc_raw(t + 20, 4, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && !(o.events & DRIVE_EVT_ARM_REFUSED));      // другого фронту немає, нового рядка немає
    CHECK(drive_arm_block(&c, APP_STATE_DISARMED, &(battery_snapshot_t){8000, t + 20, true},
                          &(crsf_intent_t){0, 0, true, true, CRSF_SW_ARM}, t + 20) == DRIVE_BLOCK_NO_EDGE);
    // Вісь тяги теж перевіряється.
    t = ready_disarmed(&c, 3000);
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, 1700, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && o.arm_block == DRIVE_BLOCK_NOT_NEUTRAL);
    // Напруга нижче порога.
    t = ready_disarmed(&c, 5000);
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(5999, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && o.arm_block == DRIVE_BLOCK_LOW_VOLTAGE && (o.events & DRIVE_EVT_ARM_REFUSED));
    // Вимірювання немає/застаріло.
    t = ready_disarmed(&c, 7000);
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t - 700), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && o.arm_block == DRIVE_BLOCK_NO_MEASUREMENT);
    // Порядок: напруга важливіша за стік (звʼязок → напруга → нейтраль → фронт).
    t = ready_disarmed(&c, 9000);
    o = go_rc(&c, APP_STATE_DISARMED, t, snap(5000, t), rc_raw(t, 3, 1500, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.arm_block == DRIVE_BLOCK_LOW_VOLTAGE);
    // Тумблер уже в ARM при появі звʼязку: фронту немає, озброєння не буде (AC-04).
    drive_ctl_init(&c);
    o = go_rc(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), rc_raw(1000, 1, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request && !(o.events & DRIVE_EVT_ARM_REFUSED));
    o = go_rc(&c, APP_STATE_DISARMED, 1020, snap(8000, 1020), rc_raw(1020, 2, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request);
    // Після втрати звʼязку з тумблером у ARM, що вже стояв: теж без озброєння.
    t = ready_disarmed(&c, 3000);
    go_rc(&c, APP_STATE_DISARMED, t + 700, snap(8000, t + 700), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);   // втрата
    CHECK(!c.link_alive);
    o = go_rc(&c, APP_STATE_DISARMED, t + 2000, snap(8000, t + 2000), rc_raw(t + 1990, 50, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(c.link_alive && !o.has_state_request);                                // перший кадр після втрати лише база
    // Без звʼязку статус: NO_LINK.
    CHECK(drive_arm_block(&c, APP_STATE_DISARMED, &(battery_snapshot_t){8000, 0, true}, &(crsf_intent_t){0}, 0) != DRIVE_BLOCK_NONE);
    // Не в DISARMED.
    CHECK(drive_arm_block(&c, APP_STATE_ARMED, &(battery_snapshot_t){8000, 0, true}, &(crsf_intent_t){0}, 0) == DRIVE_BLOCK_NOT_DISARMED);
    // Тексти.
    CHECK(strstr(drive_block_text(DRIVE_BLOCK_NOT_NEUTRAL), "нейтрал") != NULL);
    CHECK(strstr(drive_block_text(DRIVE_BLOCK_NO_EDGE), "DISARM") != NULL);
    CHECK(strstr(drive_block_text(DRIVE_BLOCK_NO_LINK), "звʼязку") != NULL);
    CHECK(strstr(drive_block_text(DRIVE_BLOCK_LOW_VOLTAGE), "напруга") != NULL);
    CHECK(strlen(drive_block_text(DRIVE_BLOCK_NONE)) > 0);
}

static void test_remote_driving(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    uint32_t t = ready_disarmed(&c, 1000);
    go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);   // озброєно
    t += 20;
    // Стік у спокої: STBY вгору, ШІМ 0 (ARMED).
    o = go_rc(&c, APP_STATE_ARMED, t, snap(8000, t), rc_raw(t, 4, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.out.stby == 1 && o.left_permille == 0 && o.right_permille == 0 && !o.has_state_request);
    // Вперед на 0.5 від ходу: обидва борти вперед, рівні (AC-01).
    o = go_rc(&c, APP_STATE_ARMED, t + 20, snap(8000, t + 20), rc_raw(t + 20, 5, NEUT, 1400, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.out.stby == 1 && o.left_permille > 0 && o.left_permille == o.right_permille);
    CHECK(o.out.left.dir1 == 1 && o.out.left.dir2 == 0 && o.out.right.dir1 == 1);
    int16_t half = o.left_permille;
    // Далі стік — швидше, до обмеження 0.75.
    o = go_rc(&c, APP_STATE_ARMED, t + 40, snap(8000, t + 40), rc_raw(t + 40, 6, NEUT, 1650, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.left_permille > half && o.left_permille <= 750);
    // Назад: обидва борти назад.
    o = go_rc(&c, APP_STATE_ARMED, t + 60, snap(8000, t + 60), rc_raw(t + 60, 7, NEUT, 400, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.left_permille < 0 && o.right_permille < 0 && o.out.left.dir1 == 0 && o.out.left.dir2 == 1);
    // Праворуч на місці: лівий вперед, правий назад.
    o = go_rc(&c, APP_STATE_ARMED, t + 80, snap(8000, t + 80), rc_raw(t + 80, 8, 1792, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.left_permille > 0 && o.right_permille < 0);
    // Ліворуч — дзеркально.
    o = go_rc(&c, APP_STATE_ARMED, t + 100, snap(8000, t + 100), rc_raw(t + 100, 9, 180, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.left_permille < 0 && o.right_permille > 0);
    // Упор і діагональ: жоден борт понад 750 (AC-02).
    uint16_t ext[] = {172, 992, 1811, 1808};
    for (int a = 0; a < 4; a++) {
        for (int b = 0; b < 4; b++) {
            o = go_rc(&c, APP_STATE_ARMED, t + 120, snap(8000, t + 120), rc_raw(t + 120, 10, ext[a], ext[b], SW_ARM), DRIVE_REQ_NONE, 0, 0);
            CHECK(o.left_permille <= 750 && o.left_permille >= -750 && o.right_permille <= 750 && o.right_permille >= -750);
            CHECK(o.out.left.duty_permille <= 750 && o.out.right.duty_permille <= 750);
        }
    }
    // Мертва зона дає нуль.
    o = go_rc(&c, APP_STATE_ARMED, t + 140, snap(8000, t + 140), rc_raw(t + 140, 11, 1000, 1030, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.left_permille == 0 && o.right_permille == 0 && o.out.stby == 1);
    // Відпущений стік зупиняє борти, ровер лишається ARMED (без запиту стану).
    CHECK(!o.has_state_request);
}

static void test_remote_disarm_paths(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    uint32_t t = ready_disarmed(&c, 1000);
    go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    t += 20;
    go_rc(&c, APP_STATE_ARMED, t, snap(8000, t), rc_raw(t, 4, NEUT, 1700, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    // Тумблер у DISARM: виходи вимкнено, запит DISARMED (AC-03).
    o = go_rc(&c, APP_STATE_ARMED, t + 20, snap(8000, t + 20), rc_raw(t + 20, 5, NEUT, 1700, SW_DIS), DRIVE_REQ_NONE, 0, 0);
    check_off(o.out);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED && o.stop == DRIVE_STOP_SWITCH_DISARM);

    // Консольний disarm у ARMED з пульта (AC-16): зупинка, DISARMED; тумблер у ARM — повторного озброєння немає.
    t = ready_disarmed(&c, 5000);
    go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    t += 20;
    go_rc(&c, APP_STATE_ARMED, t, snap(8000, t), rc_raw(t, 4, NEUT, 1700, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_ARMED, t + 20, snap(8000, t + 20), rc_raw(t + 20, 5, NEUT, 1700, SW_ARM), DRIVE_REQ_DISARM, 0, 0);
    check_off(o.out);
    CHECK(o.accepted && o.has_state_request && o.state_request == APP_STATE_DISARMED && o.stop == DRIVE_STOP_DISARM);
    o = go_rc(&c, APP_STATE_DISARMED, t + 40, snap(8000, t + 40), rc_raw(t + 40, 6, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!o.has_state_request);                                          // тумблер у ARM, фронту немає
    CHECK(drive_arm_block(&c, APP_STATE_DISARMED, &(battery_snapshot_t){8000, t + 40, true},
                          &(crsf_intent_t){0, 0, true, true, CRSF_SW_ARM}, t + 40) == DRIVE_BLOCK_NO_EDGE);
    // Тумблер вниз і знову вгору: можна озброїти.
    go_rc(&c, APP_STATE_DISARMED, t + 60, snap(8000, t + 60), rc_raw(t + 60, 7, NEUT, NEUT, SW_DIS), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_DISARMED, t + 80, snap(8000, t + 80), rc_raw(t + 80, 8, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_ARMED);

    // Відсічка по напрузі в русі з пульта: FAILSAFE «напруга» незалежно від стіка й тумблера (AC-17).
    t = ready_disarmed(&c, 9000);
    go_rc(&c, APP_STATE_DISARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    t += 20;
    go_rc(&c, APP_STATE_ARMED, t, snap(8000, t), rc_raw(t, 4, NEUT, 1700, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_ARMED, t + 20, snap(5900, t + 20), rc_raw(t + 20, 5, NEUT, 1700, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    check_off(o.out);
    CHECK(o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_VOLTAGE && o.stop == DRIVE_STOP_LOW_VOLTAGE);
}

static void test_stby_only_in_armed_remote(void)
{
    // Поза ARMED виходи завжди вимкнені, хоч би де був стік і тумблер (конституція, п. 7).
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        if (st == APP_STATE_ARMED || st == APP_STATE_OBSTACLE_HOLD) continue;   // OBSTACLE_HOLD — стан руху (phase6-tof)
        drive_ctl_t c; drive_ctl_out_t o;
        uint32_t t = ready_disarmed(&c, 1000);
        o = go_rc(&c, (app_state_t)st, t, snap(8000, t), rc_raw(t, 3, 1792, 1808, SW_ARM), DRIVE_REQ_NONE, 0, 0);
        check_off(o.out);
        CHECK(o.left_permille == 0 && o.right_permille == 0);
    }
}

// ---------- phase3-crsf T6: вихід з FAILSAFE після втрати звʼязку ----------

static drive_ctl_out_t go_fs(drive_ctl_t *c, uint32_t now, battery_snapshot_t b, crsf_snapshot_t rc, app_failsafe_cause_t cause)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(APP_STATE_FAILSAFE, now, b, DRIVE_REQ_NONE, 0, 0);
    in.rc = rc;
    in.failsafe_cause = cause;
    drive_ctl_step(c, &in, &o);
    return o;
}

// Ровер у FAILSAFE через втрату звʼязку, звʼязок щойно відновлено першим кадром у момент t.
static void failsafe_link_lost_then_back(drive_ctl_t *c, uint32_t t, uint16_t sw)
{
    drive_ctl_init(c);
    go_rc(c, APP_STATE_ARMED, 1000, snap(8000, 1000), rc_raw(1000, 1, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    go_fs(c, 1700, snap(8000, 1700), rc_raw(1000, 1, NEUT, NEUT, SW_ARM), APP_FS_LINK_LOST);   // втрата
    go_fs(c, t, snap(8000, t), rc_raw(t, 50, NEUT, NEUT, (uint16_t)sw), APP_FS_LINK_LOST);     // перший кадр після втрати
}

static void test_failsafe_exit_window(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    CHECK(c.link_alive && c.link_since_ms == 3000);
    // 999 мс — ні, 1000 мс — так (AC-08).
    o = go_fs(&c, 3999, snap(8000, 3999), rc_raw(3990, 150, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(!o.has_state_request && !(o.events & DRIVE_EVT_FAILSAFE_EXIT));
    check_off(o.out);
    o = go_fs(&c, 4000, snap(8000, 4000), rc_raw(3995, 151, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
    CHECK(o.events & DRIVE_EVT_FAILSAFE_EXIT);
    check_off(o.out);

    // Нова втрата < 1 с звʼязку: вікно з нуля (AC-09).
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    o = go_fs(&c, 3600, snap(8000, 3600), rc_raw(3000, 50, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);   // кадрів немає 600 мс
    CHECK(!c.link_alive && (o.events & DRIVE_EVT_LINK_LOST) && !o.has_state_request);
    o = go_fs(&c, 4000, snap(8000, 4000), rc_raw(3990, 90, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);   // повернувся
    CHECK(c.link_alive && c.link_since_ms == 3990);
    o = go_fs(&c, 4900, snap(8000, 4900), rc_raw(4890, 140, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(!o.has_state_request);                                       // від 3990 лише 910 мс
    o = go_fs(&c, 4990, snap(8000, 4990), rc_raw(4980, 150, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);

    // Пропущені кадри, проміжок < 500 мс: вікно не скидається.
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    o = go_fs(&c, 3450, snap(8000, 3450), rc_raw(3020, 52, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST); // 430 мс без кадру
    CHECK(c.link_alive && c.link_since_ms == 3000 && !o.has_state_request);
    o = go_fs(&c, 4000, snap(8000, 4000), rc_raw(3990, 150, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
}

// T13 (review F6): вік кадру й тривалість звʼязку — беззнакова різниця; ≥ 2^31 мс не стає «відʼємною».
static void test_link_age_unsigned(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    const uint32_t big = 0x80000000u + 10u;
    // ARMED з пульта, далі кадрів немає ≥ 2^31 мс: це втрата звʼязку, а не «живий».
    drive_ctl_init(&c);
    go_rc(&c, APP_STATE_ARMED, 1000, snap(8000, 1000), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_ARMED, 1000 + big, snap(8000, 1000 + big), rc_snap(1000, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(!c.link_alive);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_LINK_LOST);
    check_off(o.out);
    // Звʼязок живий ≥ 2^31 мс від першого кадру після втрати: вихід із FAILSAFE не блокується «стабілізацією».
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    uint32_t now = 3000 + big;
    o = go_fs(&c, now, snap(8000, now), rc_raw(now - 10, 99, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(c.link_alive && c.link_since_ms == 3000);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED && (o.events & DRIVE_EVT_FAILSAFE_EXIT));
    // Звичайне переповнення лічильника мс (now менше за мітку числом, але новіший на 20 мс) — звʼязок живий.
    drive_ctl_init(&c);
    go_rc(&c, APP_STATE_ARMED, 0xFFFFFFF0u, snap(8000, 0xFFFFFFF0u), rc_snap(0xFFFFFFF0u, 1), DRIVE_REQ_NONE, 0, 0);
    o = go_rc(&c, APP_STATE_ARMED, 4, snap(8000, 4), rc_snap(0xFFFFFFF0u, 1), DRIVE_REQ_NONE, 0, 0);
    CHECK(c.link_alive && !o.has_state_request);
}

// T14 (review F4): тумблер у status — з бази фронту кроку керування: «невідомо» у стенді й після втрати звʼязку.
static void test_switch_shown(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    char b[640];
    crsf_status_view_t v;
    drive_ctl_init(&c);
    // Стенд: кадрів не було — тумблер невідомий, а не DISARM.
    go(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), DRIVE_REQ_NONE, 0, 0);
    CHECK(drive_switch_shown(&c) == CRSF_SW_UNKNOWN);
    memset(&v, 0, sizeof v);
    v.intent.sw = drive_switch_shown(&c);
    crsf_fmt_status(b, sizeof b, &v);
    CHECK(strstr(b, "тумблер: невідомо") != NULL);
    // Живий звʼязок: показ за кадром.
    go_rc(&c, APP_STATE_DISARMED, 1020, snap(8000, 1020), rc_raw(1020, 1, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(drive_switch_shown(&c) == CRSF_SW_ARM);
    go_rc(&c, APP_STATE_DISARMED, 1040, snap(8000, 1040), rc_raw(1040, 2, NEUT, NEUT, SW_DIS), DRIVE_REQ_NONE, 0, 0);
    CHECK(drive_switch_shown(&c) == CRSF_SW_DISARM);
    // Втрата звʼязку: останній кадр казав DISARM, але база фронту невідома — показ «невідомо».
    o = go_rc(&c, APP_STATE_DISARMED, 1600, snap(8000, 1600), rc_raw(1040, 2, NEUT, NEUT, SW_DIS), DRIVE_REQ_NONE, 0, 0);
    CHECK((o.events & DRIVE_EVT_LINK_LOST) && drive_switch_shown(&c) == CRSF_SW_UNKNOWN);
}

// T17 (review F1): заборона виходів (control не підписано на Task WDT) — жодного ARMED і ШІМ ні з пульта, ні з консолі.
static drive_ctl_out_t go_inh(drive_ctl_t *c, app_state_t st, uint32_t now, crsf_snapshot_t rc, drive_req_kind_t k,
                              float thr, float steer)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(st, now, snap(8000, now), k, thr, steer);
    in.rc = rc;
    in.outputs_inhibited = true;
    drive_ctl_step(c, &in, &o);
    return o;
}

static void test_outputs_inhibited(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // Фронт тумблера за всіх умов: запиту ARMED немає.
    uint32_t t = ready_disarmed(&c, 1000);
    o = go_inh(&c, APP_STATE_DISARMED, t, rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(!(o.has_state_request && o.state_request == APP_STATE_ARMED));
    CHECK(!(o.events & DRIVE_EVT_REMOTE_ARMED));
    check_off(o.out);
    // Якщо ровер усе ж ARMED: стік до упору не дає ШІМ, STBY низька.
    o = go_inh(&c, APP_STATE_ARMED, t + 20, rc_raw(t + 20, 4, NEUT, 1811, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    check_off(o.out);
    CHECK(o.left_permille == 0 && o.right_permille == 0);
    // Консоль у стенді: arm відхилено, drive не відкриває вікна.
    drive_ctl_init(&c);
    crsf_snapshot_t none;
    memset(&none, 0, sizeof none);
    o = go_inh(&c, APP_STATE_DISARMED, 1000, none, DRIVE_REQ_ARM, 0, 0);
    CHECK(!o.accepted && !(o.has_state_request && o.state_request == APP_STATE_ARMED));
    o = go_inh(&c, APP_STATE_ARMED, 1020, none, DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    CHECK(!o.accepted && !c.window_active);
    check_off(o.out);
    // Втрата звʼязку при забороні так само веде у FAILSAFE (запити зупинки не блокуються).
    t = ready_disarmed(&c, 5000);
    go_rc(&c, APP_STATE_ARMED, t, snap(8000, t), rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    o = go_inh(&c, APP_STATE_ARMED, t + 600, rc_raw(t, 3, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE);
}

// T18 (review F5): затримка «кадр прийнято → команда бортам» для NFR ≤ 50 мс (spec §6): кількість, мін, середнє, макс.
static void test_latency_stats(void)
{
    drive_lat_t l;
    char b[160];
    drive_lat_reset(&l);
    CHECK(l.n == 0);
    drive_lat_fmt(&l, b, sizeof b);
    CHECK(strstr(b, "0 кадрів") != NULL);
    drive_lat_add(&l, 1000, 1012);
    drive_lat_add(&l, 1010, 1031);
    drive_lat_add(&l, 0xFFFFFFF8u, 3);                // переповнення мс: 11 мс, не 4 млрд; середнє 44/3 ≈ 15
    CHECK(l.n == 3 && l.min_ms == 11 && l.max_ms == 21);
    drive_lat_fmt(&l, b, sizeof b);
    CHECK(strstr(b, "3 кадрів") && strstr(b, "мін 11") && strstr(b, "сер 15") && strstr(b, "макс 21"));
    CHECK(strstr(b, "мс") != NULL && strlen(b) < sizeof b - 1);
    drive_lat_reset(&l);
    CHECK(l.n == 0 && l.max_ms == 0);
}

static void test_failsafe_exit_switch(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // Тумблер лишився в ARM: чекає навіть після 1 с (AC-09).
    failsafe_link_lost_then_back(&c, 3000, SW_ARM);
    o = go_fs(&c, 4500, snap(8000, 4500), rc_raw(4490, 150, NEUT, NEUT, SW_ARM), APP_FS_LINK_LOST);
    CHECK(!o.has_state_request);
    CHECK(drive_failsafe_exit_state(&c, APP_FS_LINK_LOST,
                                    &(crsf_intent_t){0, 0, true, true, CRSF_SW_ARM}, 4500) == DRIVE_FSX_SWITCH_ARM);
    // Тумблер щойно опущений при вже повному вікні: вихід на цьому такті.
    o = go_fs(&c, 4520, snap(8000, 4520), rc_raw(4510, 151, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED);
    // Вихід не озброює й не їде: виходи вимкнено.
    check_off(o.out);
    // Рух стіка у FAILSAFE нічого не дає.
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    o = go_fs(&c, 3500, snap(8000, 3500), rc_raw(3490, 100, 1792, 1808, SW_DIS), APP_FS_LINK_LOST);
    check_off(o.out);
    CHECK(o.left_permille == 0 && o.right_permille == 0);
}

static void test_failsafe_voltage_never_exits(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // Причина «напруга» (вже підвищена): вихід не запитується, хоч би що (AC-09).
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    o = go_fs(&c, 6000, snap(8000, 6000), rc_raw(5990, 400, NEUT, NEUT, SW_DIS), APP_FS_VOLTAGE);
    CHECK(!o.has_state_request && !(o.events & DRIVE_EVT_FAILSAFE_EXIT));
    CHECK(drive_failsafe_exit_state(&c, APP_FS_VOLTAGE, &(crsf_intent_t){0, 0, true, true, CRSF_SW_DISARM}, 6000) == DRIVE_FSX_RESET_ONLY);
    // Відсічка спрацювала, поки ровер у FAILSAFE через звʼязок: запит на підвищення причини, не вихід.
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    o = go_fs(&c, 4500, snap(5999, 4500), rc_raw(4490, 150, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_VOLTAGE);
    CHECK(o.events & DRIVE_EVT_CAUSE_UPGRADE);
    CHECK(!(o.events & DRIVE_EVT_FAILSAFE_EXIT));
    check_off(o.out);
    // Те саме правило застарілого виміру, що й в ARMED.
    failsafe_link_lost_then_back(&c, 3000, SW_DIS);
    o = go_fs(&c, 4500, snap(8000, 3000), rc_raw(4490, 150, NEUT, NEUT, SW_DIS), APP_FS_LINK_LOST);   // знімок 1500 мс тому
    CHECK(o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_VOLTAGE);
    // Після підвищення (причина вже «напруга») запит не повторюється кожного такту.
    o = go_fs(&c, 4520, snap(5999, 4520), rc_raw(4510, 151, NEUT, NEUT, SW_DIS), APP_FS_VOLTAGE);
    CHECK(!o.has_state_request);
    // FAILSAFE у стенді (відсічка без кадрів): нічого не запитується.
    drive_ctl_init(&c);
    o = go_fs(&c, 3000, snap(5000, 3000), (crsf_snapshot_t){0}, APP_FS_VOLTAGE);
    CHECK(!o.has_state_request && !c.remote_active);
}

static void test_failsafe_exit_texts(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    crsf_intent_t it = {0, 0, true, true, CRSF_SW_DISARM};
    CHECK(drive_failsafe_exit_state(&c, APP_FS_LINK_LOST, &it, 5000) == DRIVE_FSX_NO_LINK);   // звʼязку немає
    c.remote_active = true; c.link_alive = true; c.link_since_ms = 4500;
    CHECK(drive_failsafe_exit_state(&c, APP_FS_LINK_LOST, &it, 5000) == DRIVE_FSX_STABILIZING);
    CHECK(drive_failsafe_exit_state(&c, APP_FS_LINK_LOST, &it, 5500) == DRIVE_FSX_READY);
    CHECK(strstr(drive_fsx_text(DRIVE_FSX_RESET_ONLY), "скиданн") != NULL);
    CHECK(strstr(drive_fsx_text(DRIVE_FSX_SWITCH_ARM), "DISARM") != NULL);
    CHECK(strstr(drive_fsx_text(DRIVE_FSX_STABILIZING), "стабільн") != NULL);
    CHECK(strstr(drive_fsx_text(DRIVE_FSX_NO_LINK), "звʼязку") != NULL);
    CHECK(strlen(drive_fsx_text(DRIVE_FSX_READY)) > 0);
}

// ---------- phase3-crsf T11 / phase4-encoders T4: матриця «команда × стан × режим керування», 140 пар
// (phase3 spec §6, AC-15; phase4 contracts/cli.md: 10 команд × 7 станів × 2 режими) ----------

// Наскрізний результат: консоль пропускає за станом і (для arm, drive, disarm) керування приймає в цьому режимі.
static bool end_to_end_allowed(const char *line, app_state_t st, bool remote)
{
    console_cmd_t cmd = console_parse(line, st);
    if (cmd == CONSOLE_CMD_DENIED_STATE || cmd == CONSOLE_CMD_BAD_ARGS || cmd == CONSOLE_CMD_EXTRA_ARGS) {
        return false;
    }
    if (cmd != CONSOLE_CMD_ARM && cmd != CONSOLE_CMD_DRIVE && cmd != CONSOLE_CMD_DISARM) {
        return true;   // help, status, reboot, wdt-test, light, beep, enc: режим керування їх не стосується
    }
    drive_ctl_t c;
    drive_ctl_init(&c);
    if (remote) {
        go_rc(&c, APP_STATE_DISARMED, 1000, snap(8000, 1000), rc_raw(1000, 1, NEUT, NEUT, SW_ARM), DRIVE_REQ_NONE, 0, 0);
    }
    drive_req_kind_t k = cmd == CONSOLE_CMD_ARM ? DRIVE_REQ_ARM : cmd == CONSOLE_CMD_DRIVE ? DRIVE_REQ_DRIVE : DRIVE_REQ_DISARM;
    drive_ctl_out_t o;
    if (remote) {
        o = go_rc(&c, st, 1020, snap(8000, 1020), rc_raw(1010, 2, NEUT, NEUT, SW_ARM), k, 0.3f, 0.0f);
    } else {
        o = go(&c, st, 1020, snap(8000, 1020), k, 0.3f, 0.0f);
    }
    return o.accepted;
}

static void test_matrix_140(void)
{
    const char *lines[10] = {"help", "status", "reboot", "wdt-test", "arm", "disarm", "drive 0.3 0", "light head on",
                             "beep 1000 100", "enc"};
    int pairs = 0;
    for (int m = 0; m < 2; m++) {
        bool remote = m == 1;
        for (int i = 0; i < 10; i++) {
            for (int s = 0; s < APP_STATE_COUNT; s++) {
                app_state_t st = (app_state_t)s;
                bool dis = st == APP_STATE_DISARMED, arm = st == APP_STATE_ARMED, hold = st == APP_STATE_OBSTACLE_HOLD;
                bool want;
                switch (i) {
                case 0: case 1: want = true; break;                                  // help, status
                case 2: want = dis || st == APP_STATE_BOOT; break;                   // reboot
                case 3: want = dis; break;                                           // wdt-test
                case 4: want = dis && !remote; break;                                // arm: лише стенд
                case 5: want = true; break;                                          // disarm: завжди
                case 6: want = (arm || hold) && !remote; break;                          // drive: лише стенд, ARMED і OBSTACLE_HOLD
                case 7: case 8: want = dis || arm; break;                            // light head, beep
                default: want = true; break;                                         // enc: усі стани й режими
                }
                CHECK(end_to_end_allowed(lines[i], st, remote) == want);
                pairs++;
            }
        }
    }
    CHECK(pairs == 140);
    printf("ПОКРИТТЯ пар команда × стан × режим: %d з 140\n", pairs);
}

// ---------- phase5-pid T4: регулятор у кроці керування (spec AC-02, AC-13, AC-16; ADR-0001) ----------

static pid_set_t pid_on(float kp, float ki, float ramp, bool on)
{
    pid_set_t s;
    pid_set_init(&s);
    pid_set_param(&s, PID_PARAM_KP, kp);
    pid_set_param(&s, PID_PARAM_KI, ki);
    pid_set_param(&s, PID_PARAM_KD, 0.0f);
    pid_set_param(&s, PID_PARAM_RAMP, ramp);
    pid_set_mode(&s, on);
    return s;
}

static drive_enc_in_t enc_all(float rpm, uint32_t intervals, uint32_t now_ms)
{
    drive_enc_in_t e;
    memset(&e, 0, sizeof e);
    e.valid = true;
    for (int i = 0; i < ENC_WHEELS; i++) { e.avail[i] = true; e.rpm[i] = rpm; }
    e.intervals = intervals;
    e.t_us = (uint64_t)now_ms * 1000u;
    return e;
}

static drive_ctl_out_t go_pid(drive_ctl_t *c, app_state_t st, uint32_t now, drive_req_kind_t k, float thr,
                              float steer, pid_set_t pid, drive_enc_in_t enc)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(st, now, snap(8000, now), k, thr, steer);
    in.pid = pid;
    in.enc = enc;
    drive_ctl_step(c, &in, &o);
    return o;
}

// Стенд: arm і перший такт у ARMED (фіксує режим регулятора з цього такту).
static uint32_t bench_armed(drive_ctl_t *c, pid_set_t pid, uint32_t t)
{
    drive_ctl_init(c);
    go_pid(c, APP_STATE_DISARMED, t, DRIVE_REQ_ARM, 0, 0, pid, enc_all(0, 1, t));
    return t + 20;
}

static void test_pid_bench_regulated(void)
{
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.0f, 100000.0f, true);
    uint32_t t = bench_armed(&c, pid, 1000);
    // Стендова команда 0.6 → задана 90 об/хв; швидкість 60 → вихід лише P: 0.003 × 30 = 0.09 (не 0.6 мікшера).
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid, enc_all(60.0f, 2, t));
    CHECK(o.accepted);
    CHECK(o.left_permille == 90 && o.right_permille == 90);
    CHECK(o.pid_view[0].mode == PID_SIDE_TWO && o.pid_view[0].target_rpm == 90.0f);
    // Наступний такт без запиту: виходи перераховуються з новим знімком (швидкість 90 → нуль похибки → 0).
    o = go_pid(&c, APP_STATE_ARMED, t + 20, DRIVE_REQ_NONE, 0, 0, pid, enc_all(90.0f, 3, t + 20));
    CHECK(o.left_permille == 0 && o.out.stby == 1 && o.remaining_ms > 0);
    // Той самий знімок ще раз: P/I/D не оновлюються, вихід тримається.
    pid_set_t pi = pid_on(0.003f, 0.5f, 100000.0f, true);
    t = bench_armed(&c, pi, 5000);
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pi, enc_all(60.0f, 2, t));
    int16_t first = o.left_permille;
    o = go_pid(&c, APP_STATE_ARMED, t + 20, DRIVE_REQ_NONE, 0, 0, pi, enc_all(60.0f, 2, t));
    CHECK(o.left_permille == first);
    o = go_pid(&c, APP_STATE_ARMED, t + 40, DRIVE_REQ_NONE, 0, 0, pi, enc_all(60.0f, 3, t + 40));
    CHECK(o.left_permille > first);   // новий знімок — накопичена складова зросла
}

static void test_pid_off_is_phase4(void)
{
    // AC-16: регулятор вимкнений чи не налаштований — виходи точно як у фазі 4 (0.6 → 600 проміле).
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t off = pid_on(0.003f, 0.0f, 300.0f, false);
    uint32_t t = bench_armed(&c, off, 1000);
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, off, enc_all(0.0f, 2, t));
    CHECK(o.left_permille == 600 && o.right_permille == 600);
    CHECK(o.pid_view[0].mode == PID_SIDE_OPEN);
    pid_set_t none;
    pid_set_init(&none);
    t = bench_armed(&c, none, 3000);
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, -0.2f, none, enc_all(0.0f, 2, t));
    CHECK(o.left_permille == 400 && o.right_permille == 750);
}

static void test_pid_mode_latched(void)
{
    // AC-15: режим фіксується при вході в ARMED; «pid on» під час руху діє лише з наступного озброєння.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t off = pid_on(0.003f, 0.0f, 100000.0f, false);
    pid_set_t on = pid_on(0.003f, 0.0f, 100000.0f, true);
    uint32_t t = bench_armed(&c, off, 1000);
    go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_NONE, 0, 0, off, enc_all(0.0f, 2, t));   // вхід в ARMED: вимкнений
    o = go_pid(&c, APP_STATE_ARMED, t + 20, DRIVE_REQ_DRIVE, 0.6f, 0.0f, on, enc_all(60.0f, 3, t + 20));
    CHECK(o.left_permille == 600);
    go_pid(&c, APP_STATE_DISARMED, t + 40, DRIVE_REQ_NONE, 0, 0, on, enc_all(0.0f, 4, t + 40));
    go_pid(&c, APP_STATE_DISARMED, t + 60, DRIVE_REQ_ARM, 0, 0, on, enc_all(0.0f, 5, t + 60));
    o = go_pid(&c, APP_STATE_ARMED, t + 80, DRIVE_REQ_DRIVE, 0.6f, 0.0f, on, enc_all(60.0f, 6, t + 80));
    CHECK(o.left_permille == 90);
}

static void test_pid_ramp_in_step(void)
{
    // AC-13: задана зростає не швидше за темп (300 об/хв/с × 20 мс = 6 об/хв за такт); нейтраль — нуль одразу.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.01f, 0.0f, 300.0f, true);
    uint32_t t = bench_armed(&c, pid, 1000);
    go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_NONE, 0, 0, pid, enc_all(0.0f, 2, t));
    float prev = 0.0f;
    for (int k = 1; k <= 10; k++) {
        o = go_pid(&c, APP_STATE_ARMED, t + 20u * (unsigned)k, k == 1 ? DRIVE_REQ_DRIVE : DRIVE_REQ_NONE, 1.0f, 0.0f,
                   pid, enc_all(0.0f, 2u + (unsigned)k, t + 20u * (unsigned)k));
        CHECK(o.pid_view[0].target_rpm - prev <= 6.0f + 1e-3f);
        CHECK(o.pid_view[0].target_rpm > prev);
        prev = o.pid_view[0].target_rpm;
    }
    // Стендова команда з нульовою тягою — задана й вихід нуль у тому ж такті.
    o = go_pid(&c, APP_STATE_ARMED, t + 300, DRIVE_REQ_DRIVE, 0.0f, 0.0f, pid, enc_all(30.0f, 40, t + 300));
    CHECK(o.left_permille == 0 && o.right_permille == 0 && o.pid_view[0].target_rpm == 0.0f);
}

// ---------- phase5-pid T5: відкати, зупинки, збереження (spec AC-12, AC-12b, AC-14, AC-15) ----------

static void test_pid_encoder_availability(void)
{
    // AC-12: одне колесо борту недоступне — борт за другим; обидва — борт без регулятора; інший борт регулюється.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.0f, 100000.0f, true);
    uint32_t t = bench_armed(&c, pid, 1000);
    drive_enc_in_t e = enc_all(60.0f, 2, t);
    e.avail[ENC_RL] = false;        // лівий: лише FL
    e.rpm[ENC_FL] = 30.0f;
    e.avail[ENC_FR] = false;        // правий: жодного
    e.avail[ENC_RR] = false;
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid, e);
    CHECK(o.pid_view[0].mode == PID_SIDE_ONE && strcmp(o.pid_view[0].missing, "RL") == 0);
    CHECK(o.pid_view[0].speed_rpm == 30.0f);
    CHECK(o.left_permille == 180);                  // 0.003 × (90 − 30)
    CHECK(o.pid_view[1].mode == PID_SIDE_NO_ENC);
    CHECK(o.right_permille == 600);                 // без регулятора: вихід мікшера
    CHECK(!o.has_state_request);                    // FAILSAFE не настає
}

static void test_pid_stale_snapshot(void)
{
    // AC-12b: знімок старший за 100 мс — обидва борти без регулятора, подія один раз; оновлення — регулятор з нуля.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.5f, 100000.0f, true);
    uint32_t t = bench_armed(&c, pid, 1000);
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid, enc_all(60.0f, 2, t));
    CHECK(o.left_permille < 600);
    drive_enc_in_t frozen = enc_all(60.0f, 2, t);   // знімок більше не оновлюється
    o = go_pid(&c, APP_STATE_ARMED, t + 100, DRIVE_REQ_NONE, 0, 0, pid, frozen);
    CHECK(!(o.events & DRIVE_EVT_ENC_STALE));      // рівно 100 мс — ще не застарів
    o = go_pid(&c, APP_STATE_ARMED, t + 120, DRIVE_REQ_NONE, 0, 0, pid, frozen);
    CHECK(o.events & DRIVE_EVT_ENC_STALE);
    CHECK(o.left_permille == 600 && o.right_permille == 600);
    CHECK(o.pid_view[0].mode == PID_SIDE_NO_ENC && o.pid_view[1].mode == PID_SIDE_NO_ENC);
    CHECK(!o.has_state_request);
    o = go_pid(&c, APP_STATE_ARMED, t + 140, DRIVE_REQ_NONE, 0, 0, pid, frozen);
    CHECK(!(o.events & DRIVE_EVT_ENC_STALE));      // подія — один раз
    o = go_pid(&c, APP_STATE_ARMED, t + 160, DRIVE_REQ_NONE, 0, 0, pid, enc_all(60.0f, 9, t + 160));
    CHECK(o.events & DRIVE_EVT_ENC_RESTORED);
    CHECK(o.pid_view[0].mode == PID_SIDE_TWO);
    CHECK(c.pid_side[0].integ <= 0.5f * 30.0f * 0.040f + 1e-5f);   // з нуля: лише один приріст
}

static void test_pid_reset_on_stops(void)
{
    // AC-14: будь-яка зупинка скидає регулятор; після повторного озброєння зі стіком у нейтралі — нуль.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.5f, 100000.0f, true);
    uint32_t t = bench_armed(&c, pid, 1000);
    for (int k = 0; k < 10; k++) go_pid(&c, APP_STATE_ARMED, t + 20u * (unsigned)k, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid,
                                        enc_all(40.0f, 2u + (unsigned)k, t + 20u * (unsigned)k));
    CHECK(c.pid_side[0].integ > 0.0f);
    o = go_pid(&c, APP_STATE_ARMED, t + 200, DRIVE_REQ_DISARM, 0, 0, pid, enc_all(40.0f, 20, t + 200));
    CHECK(o.left_permille == 0 && o.out.stby == 0 && o.has_state_request);
    CHECK(c.pid_side[0].integ == 0.0f && c.ramp_rpm[0] == 0.0f);   // у тому ж такті
    go_pid(&c, APP_STATE_DISARMED, t + 220, DRIVE_REQ_ARM, 0, 0, pid, enc_all(0.0f, 21, t + 220));
    o = go_pid(&c, APP_STATE_ARMED, t + 240, DRIVE_REQ_NONE, 0, 0, pid, enc_all(0.0f, 22, t + 240));
    CHECK(o.left_permille == 0 && o.right_permille == 0);
    // Відсічка по напрузі в ARMED під час руху: виходи вимкнено й регулятор скинуто в тому ж такті.
    t = bench_armed(&c, pid, 5000);
    for (int k = 0; k < 5; k++) go_pid(&c, APP_STATE_ARMED, t + 20u * (unsigned)k, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid,
                                       enc_all(40.0f, 2u + (unsigned)k, t + 20u * (unsigned)k));
    drive_ctl_in_t in = in_for(APP_STATE_ARMED, t + 100, snap(5000, t + 100), DRIVE_REQ_NONE, 0, 0);
    in.pid = pid;
    in.enc = enc_all(40.0f, 7, t + 100);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE && o.out.stby == 0);
    CHECK(c.pid_side[0].integ == 0.0f);
}

static void test_pid_saving_blocks_arm(void)
{
    // AC-15: озброєння, що збіглося із записом набору, відхиляється з причиною «триває збереження набору».
    drive_ctl_t c;
    drive_ctl_init(&c);
    drive_ctl_in_t in = in_for(APP_STATE_DISARMED, 1000, snap(8000, 1000), DRIVE_REQ_ARM, 0, 0);
    in.pid_saving = true;
    drive_ctl_out_t o;
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.accepted && o.reason == DRIVE_REASON_PID_SAVING && !o.has_state_request);
    char buf[160];
    drive_reason_text(DRIVE_REASON_PID_SAVING, 0, buf, sizeof buf);
    CHECK(strstr(buf, "триває збереження набору") != NULL);
    CHECK(strstr(drive_block_text(DRIVE_BLOCK_PID_SAVING), "триває збереження набору") != NULL);
    // Тумблер пульта: фронт під час збереження — відмова з причиною.
    uint32_t t = ready_disarmed(&c, 2000);
    in = in_for(APP_STATE_DISARMED, t, snap(8000, t), DRIVE_REQ_NONE, 0, 0);
    in.rc = rc_raw(t, 3, NEUT, NEUT, SW_ARM);
    in.pid_saving = true;
    drive_ctl_step(&c, &in, &o);
    CHECK(!o.has_state_request && (o.events & DRIVE_EVT_ARM_REFUSED) && o.arm_block == DRIVE_BLOCK_PID_SAVING);
}

static void test_pid_mode_change_reason(void)
{
    // Рев'ю R1 (AC-15): озброєння, що збіглося зі зміною режиму, відхиляється з власною причиною, а не «збереження».
    char buf[160];
    drive_reason_text(DRIVE_REASON_PID_MODE, 0, buf, sizeof buf);
    CHECK(strstr(buf, "зміна режиму регулятора") != NULL && strstr(buf, "збереження") == NULL);
    const char *b = drive_block_text(DRIVE_BLOCK_PID_MODE);
    CHECK(strstr(b, "зміна режиму регулятора") != NULL && strstr(b, "збереження") == NULL);
}

static void test_pid_arm_refused(void)
{
    // Рев'ю S1 (AC-15): дозвіл на ARMED не отримано — запит знято, причина відповідає тому, що заважало.
    drive_ctl_t c;
    drive_ctl_out_t o;
    drive_ctl_init(&c);
    drive_ctl_in_t in = in_for(APP_STATE_DISARMED, 1000, snap(8000, 1000), DRIVE_REQ_ARM, 0, 0);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.accepted && o.has_state_request && o.state_request == APP_STATE_ARMED);
    drive_ctl_out_t o2 = o;
    drive_arm_refused(&o, true, PID_BUSY_MODE);
    CHECK(!o.has_state_request && !o.accepted && o.reason == DRIVE_REASON_PID_MODE);
    drive_arm_refused(&o2, true, PID_BUSY_SAVE);
    CHECK(!o2.has_state_request && !o2.accepted && o2.reason == DRIVE_REASON_PID_SAVING);
    // Тумблер пульта: подія «озброєно» стає «відмова» з причиною.
    uint32_t t = ready_disarmed(&c, 2000);
    in = in_for(APP_STATE_DISARMED, t, snap(8000, t), DRIVE_REQ_NONE, 0, 0);
    in.rc = rc_raw(t, 3, NEUT, NEUT, SW_ARM);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.has_state_request && (o.events & DRIVE_EVT_REMOTE_ARMED));
    o2 = o;
    drive_arm_refused(&o, false, PID_BUSY_MODE);
    CHECK(!o.has_state_request && !(o.events & DRIVE_EVT_REMOTE_ARMED) && (o.events & DRIVE_EVT_ARM_REFUSED));
    CHECK(o.arm_block == DRIVE_BLOCK_PID_MODE);
    drive_arm_refused(&o2, false, PID_BUSY_SAVE);
    CHECK(!o2.has_state_request && (o2.events & DRIVE_EVT_ARM_REFUSED) && o2.arm_block == DRIVE_BLOCK_PID_SAVING);
}

static void test_pid_view_disarmed(void)
{
    // Рев'ю F2 (AC-03, AC-06, AC-12): поза ARMED показ бере режим поточного набору, а не зафіксований при озброєнні.
    drive_ctl_t c;
    drive_ctl_out_t o;
    drive_ctl_init(&c);
    pid_set_t on = pid_on(0.003f, 0.0f, 300.0f, true);
    drive_enc_in_t e = enc_all(0.0f, 2, 1000);
    e.avail[ENC_RL] = false;                       // лівий: лише FL
    e.avail[ENC_FR] = false;                       // правий: жодного
    e.avail[ENC_RR] = false;
    // Старт зі збереженим «увімкненим» набором (AC-06), ровер ще не озброювали.
    o = go_pid(&c, APP_STATE_DISARMED, 1000, DRIVE_REQ_NONE, 0, 0, on, e);
    CHECK(o.pid_view[0].mode == PID_SIDE_ONE && o.pid_view[0].missing && strcmp(o.pid_view[0].missing, "RL") == 0);
    CHECK(o.pid_view[1].mode == PID_SIDE_NO_ENC);
    o = go_pid(&c, APP_STATE_DISARMED, 1020, DRIVE_REQ_NONE, 0, 0, on, enc_all(0.0f, 3, 1020));
    CHECK(o.pid_view[0].mode == PID_SIDE_TWO && o.pid_view[1].mode == PID_SIDE_TWO);
    // Озброєння з увімкненим, потім «pid off» у DISARMED — показ одразу «без регулятора».
    go_pid(&c, APP_STATE_DISARMED, 1040, DRIVE_REQ_ARM, 0, 0, on, enc_all(0.0f, 4, 1040));
    go_pid(&c, APP_STATE_ARMED, 1060, DRIVE_REQ_NONE, 0, 0, on, enc_all(0.0f, 5, 1060));
    go_pid(&c, APP_STATE_DISARMED, 1080, DRIVE_REQ_NONE, 0, 0, on, enc_all(0.0f, 6, 1080));
    pid_set_t off = pid_on(0.003f, 0.0f, 300.0f, false);
    o = go_pid(&c, APP_STATE_DISARMED, 1100, DRIVE_REQ_NONE, 0, 0, off, enc_all(0.0f, 7, 1100));
    CHECK(o.pid_view[0].mode == PID_SIDE_OPEN && o.pid_view[1].mode == PID_SIDE_OPEN);
    // У ARMED діє зафіксований режим: «pid on» під час руху не змінює показ до наступного озброєння (AC-15).
    go_pid(&c, APP_STATE_DISARMED, 1120, DRIVE_REQ_ARM, 0, 0, off, enc_all(0.0f, 8, 1120));
    go_pid(&c, APP_STATE_ARMED, 1140, DRIVE_REQ_NONE, 0, 0, off, enc_all(0.0f, 9, 1140));   // вхід в ARMED: вимкнений
    o = go_pid(&c, APP_STATE_ARMED, 1160, DRIVE_REQ_NONE, 0, 0, on, enc_all(0.0f, 10, 1160));
    CHECK(o.pid_view[0].mode == PID_SIDE_OPEN);
}

// ---------- phase5-pid, рев'ю F6: регулятор на шляху пульта і при всіх зупинках (spec AC-14, AC-16, AC-18) ----------

static drive_ctl_out_t go_rc_pid(drive_ctl_t *c, app_state_t st, uint32_t now, crsf_snapshot_t rc, pid_set_t pid,
                                 drive_enc_in_t enc)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(st, now, snap(8000, now), DRIVE_REQ_NONE, 0, 0);
    in.rc = rc;
    in.pid = pid;
    in.enc = enc;
    drive_ctl_step(c, &in, &o);
    return o;
}

// Озброєння з пульта фронтом тумблера і перший такт у ARMED (фіксує режим регулятора); повертає наступну мітку.
static uint32_t remote_armed(drive_ctl_t *c, pid_set_t pid, uint32_t t0)
{
    uint32_t t = ready_disarmed(c, t0);
    drive_ctl_out_t o = go_rc_pid(c, APP_STATE_DISARMED, t, rc_raw(t, 3, NEUT, NEUT, SW_ARM), pid, enc_all(0, 1, t));
    CHECK(o.has_state_request && o.state_request == APP_STATE_ARMED);
    go_rc_pid(c, APP_STATE_ARMED, t + 20, rc_raw(t + 20, 4, NEUT, NEUT, SW_ARM), pid, enc_all(0, 2, t + 20));
    return t + 40;
}

// Ровер їде з пульта стіком 1650 і накопичує складову I (швидкість 40 об/хв нижча за задану); повертає наступну мітку.
static uint32_t remote_driving(drive_ctl_t *c, pid_set_t pid, uint32_t t0)
{
    uint32_t t = remote_armed(c, pid, t0);
    for (unsigned k = 0; k < 10; k++) {
        go_rc_pid(c, APP_STATE_ARMED, t, rc_raw(t, 10 + k, NEUT, 1650, SW_ARM), pid, enc_all(40.0f, 10 + k, t));
        t += 20;
    }
    CHECK(c->pid_side[0].integ > 0.0f && c->ramp_rpm[0] > 0.0f);
    return t;
}

static void test_pid_remote_regulated(void)
{
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.0f, 100000.0f, true);
    uint32_t t = remote_armed(&c, pid, 1000);
    // Стік уперед: вихід — від регулятора (P за похибкою), а не вихід мікшера, як у фазі 4.
    crsf_snapshot_t rc = rc_raw(t, 5, NEUT, 1650, SW_ARM);
    o = go_rc_pid(&c, APP_STATE_ARMED, t, rc, pid, enc_all(60.0f, 3, t));
    CHECK(o.pid_view[0].mode == PID_SIDE_TWO && o.pid_view[1].mode == PID_SIDE_TWO);
    float mix = o.pid_view[0].mix;
    CHECK(mix > 0.0f);
    CHECK(o.pid_view[0].target_rpm == pid_target_from_mix(mix));
    int expect = (int)(0.003f * (pid_target_from_mix(mix) - 60.0f) * 1000.0f + 0.5f);
    CHECK(o.left_permille >= expect - 1 && o.left_permille <= expect + 1);
    CHECK(o.left_permille < (int)(mix * 1000.0f) - 1);   // не вихід мікшера
    CHECK(o.out.stby == 1);
    // Стік у нейтраль: задана й вихід — нуль у тому ж такті, стан регулятора скинуто.
    o = go_rc_pid(&c, APP_STATE_ARMED, t + 20, rc_raw(t + 20, 6, NEUT, NEUT, SW_ARM), pid, enc_all(60.0f, 4, t + 20));
    CHECK(o.left_permille == 0 && o.right_permille == 0);
    CHECK(c.pid_side[0].integ == 0.0f && c.ramp_rpm[0] == 0.0f);
}

static void test_pid_remote_equals_bench(void)
{
    // AC-16: однаковий вихід мікшера з пульта і зі стенду дає однакову задану.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.0f, 100000.0f, true);
    uint32_t t = remote_armed(&c, pid, 1000);
    crsf_snapshot_t rc = rc_raw(t, 5, 1200, 1650, SW_ARM);
    crsf_intent_t it;
    crsf_intent(&rc, &it);
    o = go_rc_pid(&c, APP_STATE_ARMED, t, rc, pid, enc_all(30.0f, 3, t));
    float remote_l = o.pid_view[0].target_rpm, remote_r = o.pid_view[1].target_rpm;
    t = bench_armed(&c, pid, 9000);
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, it.throttle, it.steer, pid, enc_all(30.0f, 2, t));
    CHECK(o.accepted);
    CHECK(o.pid_view[0].target_rpm == remote_l && o.pid_view[1].target_rpm == remote_r);
    CHECK(remote_l != remote_r);   // з поворотом — борти різні, тож рівність не випадкова
}

static void test_pid_reset_on_remote_stops(void)
{
    // AC-14, AC-18: кожна зупинка фаз 1–3 скидає регулятор у тому ж такті.
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.5f, 100000.0f, true);
    // Тумблер DISARM.
    uint32_t t = remote_driving(&c, pid, 1000);
    o = go_rc_pid(&c, APP_STATE_ARMED, t, rc_raw(t, 40, NEUT, 1650, SW_DIS), pid, enc_all(40.0f, 40, t));
    CHECK(o.stop == DRIVE_STOP_SWITCH_DISARM && o.out.stby == 0 && o.left_permille == 0);
    CHECK(c.pid_side[0].integ == 0.0f && c.ramp_rpm[0] == 0.0f && c.pid_side[1].integ == 0.0f);
    // Втрата звʼязку понад 500 мс: FAILSAFE, виходи вимкнено, регулятор скинуто.
    t = remote_driving(&c, pid, 5000);
    uint32_t last = t - 20;
    o = go_rc_pid(&c, APP_STATE_ARMED, last + 501, rc_raw(last, 19, NEUT, 1650, SW_ARM), pid, enc_all(40.0f, 41, last + 501));
    CHECK(o.stop == DRIVE_STOP_LINK_LOST && o.state_request == APP_STATE_FAILSAFE && o.out.stby == 0);
    CHECK(c.pid_side[0].integ == 0.0f && c.ramp_rpm[0] == 0.0f);
}

static void test_pid_reset_on_bench_stops(void)
{
    drive_ctl_t c;
    drive_ctl_out_t o;
    pid_set_t pid = pid_on(0.003f, 0.5f, 100000.0f, true);
    // Спливання вікна стендової команди.
    uint32_t t = bench_armed(&c, pid, 1000);
    o = go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid, enc_all(40.0f, 2, t));
    CHECK(o.accepted);
    uint32_t k = 1;
    for (; 20u * k < DRIVE_WINDOW_MS; k++) {
        o = go_pid(&c, APP_STATE_ARMED, t + 20u * k, DRIVE_REQ_NONE, 0, 0, pid, enc_all(40.0f, 2 + k, t + 20u * k));
    }
    CHECK(c.pid_side[0].integ > 0.0f && o.out.stby == 1);
    o = go_pid(&c, APP_STATE_ARMED, t + DRIVE_WINDOW_MS, DRIVE_REQ_NONE, 0, 0, pid, enc_all(40.0f, 2 + k, t + DRIVE_WINDOW_MS));
    CHECK(o.stop == DRIVE_STOP_WINDOW_EXPIRED && o.out.stby == 0 && o.left_permille == 0);
    CHECK(c.pid_side[0].integ == 0.0f && c.ramp_rpm[0] == 0.0f);
    // control без Task WDT: виходи вимкнені, регулятор не крокує і скинутий.
    t = bench_armed(&c, pid, 9000);
    go_pid(&c, APP_STATE_ARMED, t, DRIVE_REQ_DRIVE, 0.6f, 0.0f, pid, enc_all(40.0f, 2, t));
    go_pid(&c, APP_STATE_ARMED, t + 20, DRIVE_REQ_NONE, 0, 0, pid, enc_all(40.0f, 3, t + 20));
    CHECK(c.pid_side[0].integ > 0.0f);
    drive_ctl_in_t in = in_for(APP_STATE_ARMED, t + 40, snap(8000, t + 40), DRIVE_REQ_NONE, 0, 0);
    in.pid = pid;
    in.enc = enc_all(40.0f, 4, t + 40);
    in.outputs_inhibited = true;
    drive_ctl_step(&c, &in, &o);
    CHECK(o.out.stby == 0 && o.left_permille == 0 && o.right_permille == 0);
    CHECK(c.pid_side[0].integ == 0.0f && c.ramp_rpm[0] == 0.0f);
}

static void test_pid_boards_text(void)
{
    // Рев'ю F8 (AC-12): журнал старту називає борт, що працює без регулятора, і недоступні колеса.
    char buf[240];
    bool av[ENC_WHEELS] = {true, false, false, false};   // FL — так; FR, RL, RR — ні
    size_t len = drive_pid_boards_text(PID_MODE_ON, av, buf, sizeof buf);
    CHECK(len == strlen(buf));
    CHECK(strstr(buf, "лівий борт: за одним колесом (RL недоступне)") != NULL);
    CHECK(strstr(buf, "правий борт: без регулятора — немає доступного енкодера (FR, RR)") != NULL);
    bool all[ENC_WHEELS] = {true, true, true, true};
    drive_pid_boards_text(PID_MODE_ON, all, buf, sizeof buf);
    CHECK(strstr(buf, "лівий борт: за двома колесами") != NULL && strstr(buf, "правий борт: за двома колесами") != NULL);
    drive_pid_boards_text(PID_MODE_UNCONFIGURED, all, buf, sizeof buf);
    CHECK(strstr(buf, "лівий борт: без регулятора (регулятор не налаштований)") != NULL);
    CHECK(strstr(buf, "правий борт: без регулятора (регулятор не налаштований)") != NULL);
    drive_pid_boards_text(PID_MODE_OFF, av, buf, sizeof buf);
    CHECK(strstr(buf, "правий борт: без регулятора — немає доступного енкодера (FR, RR)") != NULL);
    CHECK(strstr(buf, "лівий борт: без регулятора (регулятор вимкнений)") != NULL);
}

// ───────── phase6-tof T5: обмеження тяги за далекомірами, переходи ARMED ↔ OBSTACLE_HOLD, застарілий знімок ─────────
#include "tof_calib.h"

// Знімок далекомірів: валідний, мітка t, передній/задній сектор зайняті або ні.
static tof_view_t tv(uint32_t t, bool front_occ, bool rear_occ)
{
    tof_view_t v;
    memset(&v, 0, sizeof(v));
    v.valid = true;
    v.t_ms = t;
    v.protection = true;
    v.sector[TOF_SECTOR_FRONT].guarded = true;
    v.sector[TOF_SECTOR_FRONT].occupied = front_occ;
    v.sector[TOF_SECTOR_FRONT].mm = front_occ ? 120 : 1000;
    v.sector[TOF_SECTOR_FRONT].nearest = TOF_FRONT;
    v.sector[TOF_SECTOR_REAR].guarded = true;
    v.sector[TOF_SECTOR_REAR].occupied = rear_occ;
    v.sector[TOF_SECTOR_REAR].mm = rear_occ ? 90 : 1000;
    v.sector[TOF_SECTOR_REAR].nearest = TOF_REAR;
    return v;
}

static drive_ctl_out_t gt(drive_ctl_t *c, app_state_t st, uint32_t now, tof_view_t v, drive_req_kind_t k, float thr, float steer)
{
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(st, now, snap(8000, now), k, thr, steer);
    in.tof = v;
    drive_ctl_step(c, &in, &o);
    return o;
}

static void test_tof_front_blocks_forward_and_requests_hold(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    drive_ctl_out_t o = gt(&c, APP_STATE_ARMED, 1000, tv(1000, false, false), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    CHECK(o.accepted && o.out.stby == 1 && o.left_permille == 500);       // вільно: їде
    CHECK(!o.has_state_request);
    o = gt(&c, APP_STATE_ARMED, 1020, tv(1020, true, false), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.out.stby == 1);                                                // STBY піднятий (рух назад лишається можливим)
    CHECK(o.left_permille == 0 && o.right_permille == 0);                  // тяга вперед обнулена в тому ж такті
    CHECK(o.out.left.duty_permille == 0 && o.out.right.duty_permille == 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_OBSTACLE_HOLD);
    CHECK((o.events & DRIVE_EVT_HOLD_ENTER) && o.hold_sector == TOF_SECTOR_FRONT && o.hold_mm == 120 && o.hold_nearest == TOF_FRONT);
    CHECK(o.remaining_ms > 0);                                             // вікно команди руху ще відкрите
}

static void test_tof_turn_in_place_and_reverse_in_hold(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    drive_ctl_out_t o = gt(&c, APP_STATE_ARMED, 1000, tv(1000, true, false), DRIVE_REQ_DRIVE, 0.5f, 0.4f);
    CHECK(o.accepted);
    CHECK(o.left_permille == 400 && o.right_permille == -400);             // тяга нуль, поворот лишився: розворот на місці
    // в OBSTACLE_HOLD: стік назад (стендова команда), задній вільний → їде назад
    o = gt(&c, APP_STATE_OBSTACLE_HOLD, 1020, tv(1020, true, false), DRIVE_REQ_DRIVE, -0.5f, 0.0f);
    CHECK(o.accepted && o.reason == DRIVE_REASON_NONE);
    CHECK(o.left_permille == -500 && o.right_permille == -500 && o.out.stby == 1);
    CHECK(!o.has_state_request);                                           // передній ще зайнятий: лишаємось
    // вперед в OBSTACLE_HOLD — нуль
    o = gt(&c, APP_STATE_OBSTACLE_HOLD, 1040, tv(1040, true, false), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    CHECK(o.accepted && o.left_permille == 0 && o.right_permille == 0 && o.out.stby == 1);
}

static void test_tof_release_requests_armed(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    gt(&c, APP_STATE_OBSTACLE_HOLD, 1000, tv(1000, true, false), DRIVE_REQ_DRIVE, -0.3f, 0.0f);
    drive_ctl_out_t o = gt(&c, APP_STATE_OBSTACLE_HOLD, 1020, tv(1020, false, false), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_ARMED);
    CHECK((o.events & DRIVE_EVT_HOLD_EXIT));
    CHECK(o.left_permille == -300 && o.out.stby == 1);                     // рух назад не перервано
}

static void test_tof_rear_blocks_reverse_only(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    drive_ctl_out_t o = gt(&c, APP_STATE_ARMED, 1000, tv(1000, false, true), DRIVE_REQ_DRIVE, -0.5f, 0.0f);
    CHECK(o.left_permille == 0 && o.right_permille == 0 && o.out.stby == 1);
    CHECK(o.has_state_request && o.state_request == APP_STATE_OBSTACLE_HOLD);
    CHECK(o.hold_sector == TOF_SECTOR_REAR && o.hold_mm == 90 && o.hold_nearest == TOF_REAR);
    drive_ctl_init(&c);
    o = gt(&c, APP_STATE_OBSTACLE_HOLD, 2000, tv(2000, false, true), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    CHECK(o.left_permille == 500 && o.right_permille == 500);              // уперед дозволено
    // обидва зайняті: лише поворот на місці
    drive_ctl_init(&c);
    o = gt(&c, APP_STATE_OBSTACLE_HOLD, 3000, tv(3000, true, true), DRIVE_REQ_DRIVE, 0.5f, 0.3f);
    CHECK(o.left_permille == 300 && o.right_permille == -300);
    CHECK(!o.has_state_request);
}

static void test_tof_pult_path_and_power_limit(void)
{
    // пульт: стік уперед на повну, передній сектор зайнятий → нуль; потужність ніколи понад 750
    drive_ctl_t c;
    drive_ctl_init(&c);
    uint32_t t0 = ready_disarmed(&c, 1000);
    uint32_t seq = 10;
    drive_ctl_out_t o;
    for (uint32_t t = t0; t < t0 + 100; t += 20) {
        drive_ctl_in_t in = in_for(APP_STATE_ARMED, t, snap(8000, t), DRIVE_REQ_NONE, 0, 0);
        in.rc = rc_raw(t, seq++, NEUT, 1792, SW_ARM);
        in.tof = tv(t, t >= t0 + 60, false);
        drive_ctl_step(&c, &in, &o);
        CHECK(o.out.left.duty_permille <= 750 && o.out.right.duty_permille <= 750);
        if (t < t0 + 60) {
            CHECK(o.left_permille > 0 && o.right_permille > 0);
        } else {
            CHECK(o.left_permille == 0 && o.right_permille == 0 && o.out.stby == 1);
            CHECK(o.has_state_request && o.state_request == APP_STATE_OBSTACLE_HOLD);
        }
    }
}

static void test_tof_stale_snapshot(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    gt(&c, APP_STATE_ARMED, 1000, tv(1000, false, false), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    // знімок старший за TOF_STALE_MS (250): сектори не охороняються, подія один раз, ровер їде
    drive_ctl_out_t o = gt(&c, APP_STATE_ARMED, 1260, tv(1000, true, true), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.events & DRIVE_EVT_TOF_STALE);
    CHECK(o.left_permille == 500 && !o.has_state_request);                 // навіть «зайнятий» у старому знімку ігнорується
    o = gt(&c, APP_STATE_ARMED, 1280, tv(1000, true, true), DRIVE_REQ_NONE, 0, 0);
    CHECK(!(o.events & DRIVE_EVT_TOF_STALE));                              // повторно не повідомляє
    // рівно 250 мс — ще свіжий
    drive_ctl_init(&c);
    gt(&c, APP_STATE_ARMED, 3000, tv(3000, false, false), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    o = gt(&c, APP_STATE_ARMED, 3250, tv(3000, true, false), DRIVE_REQ_NONE, 0, 0);
    CHECK(!(o.events & DRIVE_EVT_TOF_STALE));
    CHECK(o.has_state_request && o.state_request == APP_STATE_OBSTACLE_HOLD);
    // відновлення
    drive_ctl_init(&c);
    gt(&c, APP_STATE_ARMED, 5000, tv(1000, false, false), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    o = gt(&c, APP_STATE_ARMED, 5020, tv(5020, false, false), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.events & DRIVE_EVT_TOF_RESTORED);
    // знімок не отримано зовсім (valid == false): те саме, що застарілий
    drive_ctl_init(&c);
    drive_ctl_in_t in = in_for(APP_STATE_ARMED, 7000, snap(8000, 7000), DRIVE_REQ_NONE, 0, 0);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.events & DRIVE_EVT_TOF_STALE);
    // у OBSTACLE_HOLD застарілий знімок = сектори вільні → запит ARMED
    drive_ctl_init(&c);
    o = gt(&c, APP_STATE_OBSTACLE_HOLD, 9000, tv(1000, true, false), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_ARMED);
}

static void test_tof_priority_of_stops(void)
{
    drive_ctl_t c;
    drive_ctl_init(&c);
    // напруга нижче порога має перевагу над запитом OBSTACLE_HOLD
    drive_ctl_out_t o;
    drive_ctl_in_t in = in_for(APP_STATE_ARMED, 1000, snap(5000, 1000), DRIVE_REQ_NONE, 0, 0);
    in.tof = tv(1000, true, false);
    drive_ctl_step(&c, &in, &o);
    CHECK(o.has_state_request && o.state_request == APP_STATE_FAILSAFE && o.failsafe_cause == APP_FS_VOLTAGE);
    check_off(o.out);
    // спливання вікна команди руху (2 с) — DISARMED, а не OBSTACLE_HOLD
    drive_ctl_init(&c);
    gt(&c, APP_STATE_ARMED, 1000, tv(1000, false, false), DRIVE_REQ_DRIVE, 0.5f, 0.0f);
    o = gt(&c, APP_STATE_ARMED, 3000, tv(3000, true, false), DRIVE_REQ_NONE, 0, 0);
    CHECK(o.has_state_request && o.state_request == APP_STATE_DISARMED && o.stop == DRIVE_STOP_WINDOW_EXPIRED);
    check_off(o.out);
}

static void test_tof_not_in_other_states(void)
{
    // поза ARMED і OBSTACLE_HOLD зайнятий сектор станів не міняє
    const app_state_t st[] = {APP_STATE_BOOT, APP_STATE_SELFTEST, APP_STATE_DISARMED, APP_STATE_FAILSAFE, APP_STATE_FAULT};
    for (size_t i = 0; i < sizeof(st) / sizeof(st[0]); i++) {
        drive_ctl_t c;
        drive_ctl_init(&c);
        drive_ctl_out_t o = gt(&c, st[i], 1000, tv(1000, true, true), DRIVE_REQ_NONE, 0, 0);
        CHECK(!(o.has_state_request && o.state_request == APP_STATE_OBSTACLE_HOLD));
        check_off(o.out);
    }
}

int main(void)
{
    test_pid_boards_text();
    test_pid_remote_regulated();
    test_pid_remote_equals_bench();
    test_pid_reset_on_remote_stops();
    test_pid_reset_on_bench_stops();
    test_pid_view_disarmed();
    test_pid_bench_regulated();
    test_pid_off_is_phase4();
    test_pid_mode_latched();
    test_pid_ramp_in_step();
    test_pid_encoder_availability();
    test_pid_stale_snapshot();
    test_pid_reset_on_stops();
    test_pid_saving_blocks_arm();
    test_pid_mode_change_reason();
    test_pid_arm_refused();
    test_mix();
    test_outputs_from();
    test_cap();
    test_zero_side_and_off();
    test_arm();
    test_drive_req();
    test_disarm_req();
    test_ordering();
    test_window();
    test_window_repeat_and_reject();
    test_cutoff();
    test_min_mv_and_wrap();
    test_all_states_off();
    test_reason_text();
    test_stop_text_and_disarm_min();
    test_future_snapshot_and_texts();
    test_bench_without_frames_is_phase1();
    test_bench_ends_on_first_frame();
    test_console_refused_after_bench();
    test_link_timer();
    test_link_stop_texts();
    test_remote_arm_edge();
    test_remote_arm_refusals();
    test_remote_driving();
    test_remote_disarm_paths();
    test_stby_only_in_armed_remote();
    test_failsafe_exit_window();
    test_failsafe_exit_switch();
    test_link_age_unsigned();
    test_switch_shown();
    test_outputs_inhibited();
    test_latency_stats();
    test_failsafe_voltage_never_exits();
    test_failsafe_exit_texts();
    test_matrix_140();
    test_tof_front_blocks_forward_and_requests_hold();
    test_tof_turn_in_place_and_reverse_in_hold();
    test_tof_release_requests_armed();
    test_tof_rear_blocks_reverse_only();
    test_tof_pult_path_and_power_limit();
    test_tof_stale_snapshot();
    test_tof_priority_of_stops();
    test_tof_not_in_other_states();
    return test_summary();
}
