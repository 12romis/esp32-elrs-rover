// Хостові тести drive_core (spec AC-03, AC-04, AC-10, AC-20, AC-21 — T2; решта — T3, T4).
#include <stdint.h>

#include <math.h>
#include <string.h>

#include "battery_calib.h"
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
    // текст відповіді на disarm (AC-09)
    char b[160];
    drive_disarm_text(APP_STATE_ARMED, true, APP_STATE_DISARMED, DRIVE_STOP_DISARM, b, sizeof b);
    CHECK(strstr(b, "роззброєно") != NULL);
    drive_disarm_text(APP_STATE_DISARMED, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, b, sizeof b);
    CHECK(strstr(b, "DISARMED") != NULL && strstr(b, "лише з ARMED") != NULL);
    drive_disarm_text(APP_STATE_FAILSAFE, false, APP_STATE_DISARMED, DRIVE_STOP_DISARM, b, sizeof b);
    CHECK(strstr(b, "FAILSAFE") != NULL && strstr(b, "лише з ARMED") != NULL);
    drive_disarm_text(APP_STATE_ARMED, true, APP_STATE_FAILSAFE, DRIVE_STOP_LOW_VOLTAGE, b, sizeof b);
    CHECK(strstr(b, "відсічка") != NULL && strstr(b, "FAILSAFE") != NULL);
    drive_reason_text(DRIVE_REASON_TRANSITION_REFUSED, 0, b, sizeof b);
    CHECK(strstr(b, "перехід") != NULL);
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
    return test_summary();
}
