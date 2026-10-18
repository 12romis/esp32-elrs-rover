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
        CHECK(!(o.has_state_request && o.state_request == APP_STATE_ARMED));
    }
}

static void test_drive_req(void)
{
    drive_ctl_t c; drive_ctl_out_t o;
    // drive не в ARMED (AC-06): відхилено, виходи вимкнені
    for (int st = 0; st < APP_STATE_COUNT; st++) {
        if (st == APP_STATE_ARMED) continue;
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
        } else {
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
            if (o.has_state_request) CHECK(o.out.stby == 0);
            if (st != APP_STATE_ARMED) check_off(o.out);   // AC-08: поза ARMED виходи вимкнені
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
        if (st == APP_STATE_ARMED) continue;
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
        if (st == APP_STATE_ARMED) continue;
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

// ---------- phase3-crsf T11: матриця «команда × стан × режим керування», 126 пар (spec §6, AC-15) ----------

// Наскрізний результат: консоль пропускає за станом і (для arm, drive, disarm) керування приймає в цьому режимі.
static bool end_to_end_allowed(const char *line, app_state_t st, bool remote)
{
    console_cmd_t cmd = console_parse(line, st);
    if (cmd == CONSOLE_CMD_DENIED_STATE || cmd == CONSOLE_CMD_BAD_ARGS || cmd == CONSOLE_CMD_EXTRA_ARGS) {
        return false;
    }
    if (cmd != CONSOLE_CMD_ARM && cmd != CONSOLE_CMD_DRIVE && cmd != CONSOLE_CMD_DISARM) {
        return true;   // help, status, reboot, wdt-test, light, beep: режим керування їх не стосується
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

static void test_matrix_126(void)
{
    const char *lines[9] = {"help", "status", "reboot", "wdt-test", "arm", "disarm", "drive 0.3 0", "light head on", "beep 1000 100"};
    int pairs = 0;
    for (int m = 0; m < 2; m++) {
        bool remote = m == 1;
        for (int i = 0; i < 9; i++) {
            for (int s = 0; s < APP_STATE_COUNT; s++) {
                app_state_t st = (app_state_t)s;
                bool dis = st == APP_STATE_DISARMED, arm = st == APP_STATE_ARMED;
                bool want;
                switch (i) {
                case 0: case 1: want = true; break;                                  // help, status
                case 2: want = dis || st == APP_STATE_BOOT; break;                   // reboot
                case 3: want = dis; break;                                           // wdt-test
                case 4: want = dis && !remote; break;                                // arm: лише стенд
                case 5: want = true; break;                                          // disarm: завжди
                case 6: want = arm && !remote; break;                                // drive: лише стенд
                default: want = dis || arm; break;                                   // light head, beep
                }
                CHECK(end_to_end_allowed(lines[i], st, remote) == want);
                pairs++;
            }
        }
    }
    CHECK(pairs == 126);
    printf("ПОКРИТТЯ пар команда × стан × режим: %d з 126\n", pairs);
}

int main(void)
{
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
    test_failsafe_voltage_never_exits();
    test_failsafe_exit_texts();
    test_matrix_126();
    return test_summary();
}
