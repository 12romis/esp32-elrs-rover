// Хостові тести tof_core (phase6-tof T1, T2; spec AC-01, AC-03, AC-06, AC-17): медіана, сектори, зайнятість,
// звільнення, відмова датчика, обмеження тяги.
#include <string.h>

#include "test_support.h"
#include "tof_calib.h"
#include "tof_core.h"

#define THR TOF_OBSTACLE_MM

// Усі чотири датчики запущені в момент 0.
static tof_core_t started(void)
{
    tof_core_t c;
    tof_events_t ev;
    tof_core_init(&c);
    for (int i = 0; i < TOF_SENSORS; i++) {
        tof_core_start(&c, (tof_id_t)i, 0, &ev);
    }
    return c;
}

// Один цикл опитування: усі датчики дають відліки з масиву, потім tick.
static tof_events_t cycle(tof_core_t *c, uint32_t t, const uint16_t mm[TOF_SENSORS])
{
    tof_events_t ev = {0};
    for (int i = 0; i < TOF_SENSORS; i++) {
        if (mm[i] != 0xFFFF) {
            tof_core_sample(c, (tof_id_t)i, mm[i], t);
        }
    }
    tof_core_tick(c, t, &ev);
    return ev;
}

static void free_all(tof_core_t *c, uint32_t *t, int n)
{
    uint16_t far[TOF_SENSORS] = {1000, 1000, 1000, 1000};
    for (int i = 0; i < n; i++) {
        *t += TOF_PERIOD_MS;
        cycle(c, *t, far);
    }
}

static void test_constants(void)
{
    CHECK(TOF_OBSTACLE_MM == 170);
    CHECK(TOF_MEDIAN_N == 3);
    CHECK(TOF_FAIL_MS == 250);
    CHECK(tof_sector_of(TOF_LEFT_FRONT) == TOF_SECTOR_FRONT);
    CHECK(tof_sector_of(TOF_FRONT) == TOF_SECTOR_FRONT);
    CHECK(tof_sector_of(TOF_RIGHT_FRONT) == TOF_SECTOR_FRONT);
    CHECK(tof_sector_of(TOF_REAR) == TOF_SECTOR_REAR);
}

static void test_not_ready_before_three_samples(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint16_t near_[TOF_SENSORS] = {100, 100, 100, 100};
    cycle(&c, 50, near_);
    cycle(&c, 100, near_);
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state == TOF_S_NOT_READY);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(!v.sector[TOF_SECTOR_FRONT].guarded);
    cycle(&c, 150, near_);
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state == TOF_S_OK);
    CHECK(v.sensor[TOF_FRONT].mm == 100);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(v.sector[TOF_SECTOR_FRONT].mm == 100);
}

static void test_out_of_range_is_valid(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint16_t oor[TOF_SENSORS] = {8190, 8190, 8190, 8190};
    uint32_t t = 0;
    for (int i = 0; i < 4; i++) {
        t += 50;
        cycle(&c, t, oor);
    }
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state == TOF_S_OUT);
    CHECK(v.sector[TOF_SECTOR_FRONT].guarded);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(v.sector[TOF_SECTOR_FRONT].mm == TOF_OUT_OF_RANGE_MM);
    CHECK(v.protection);
}

static void test_threshold_strict(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint32_t t = 0;
    uint16_t at[TOF_SENSORS] = {THR, THR, THR, THR};
    for (int i = 0; i < 3; i++) {
        t += 50;
        cycle(&c, t, at);
    }
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);  // рівно поріг — ще вільно
    uint16_t below[TOF_SENSORS] = {THR - 1, THR - 1, THR - 1, THR - 1};
    for (int i = 0; i < 3; i++) {
        t += 50;
        cycle(&c, t, below);
    }
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(v.sector[TOF_SECTOR_REAR].occupied);
}

static void test_spike_ignored_and_two_of_three(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint32_t t = 0;
    free_all(&c, &t, 3);
    uint16_t spike[TOF_SENSORS] = {1000, 50, 1000, 1000};
    t += 50;
    cycle(&c, t, spike);          // один низький серед трьох — медіана висока
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);
    t += 50;
    cycle(&c, t, spike);          // два низьких з трьох — медіана низька
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(v.sector[TOF_SECTOR_FRONT].nearest == TOF_FRONT);
    CHECK(v.sector[TOF_SECTOR_FRONT].mm == 50);
}

static void test_release_after_three_updates(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint32_t t = 0;
    uint16_t near_[TOF_SENSORS] = {100, 100, 100, 1000};
    for (int i = 0; i < 4; i++) {
        t += 50;
        cycle(&c, t, near_);
    }
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    // медіана піднімається, коли два з трьох відліків вільні: після цього ще три оновлення поспіль
    uint16_t far[TOF_SENSORS] = {1000, 1000, 1000, 1000};
    int released_at = -1;
    for (int i = 1; i <= 8; i++) {
        t += 50;
        cycle(&c, t, far);
        tof_core_view(&c, &v);
        if (!v.sector[TOF_SECTOR_FRONT].occupied && released_at < 0) {
            released_at = i;
        }
    }
    CHECK(released_at == 4);  // перше вільне оновлення медіани на кроці 2, ще два поспіль — звільнення на кроці 4
}

static void test_release_counter_resets(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint32_t t = 0;
    uint16_t near_[TOF_SENSORS] = {100, 100, 100, 1000};
    uint16_t far[TOF_SENSORS] = {1000, 1000, 1000, 1000};
    for (int i = 0; i < 4; i++) {
        t += 50;
        cycle(&c, t, near_);
    }
    for (int i = 0; i < 2; i++) {      // перше оновлення ще з низькою медіаною, друге — перше вільне
        t += 50;
        cycle(&c, t, far);
    }
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    for (int i = 0; i < 2; i++) {      // знову близько: два низькі відліки повертають низьку медіану й скидають лічильник
        t += 50;
        cycle(&c, t, near_);
    }
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    for (int i = 0; i < 3; i++) {      // після скидання потрібні ще три вільні оновлення, а не одне
        t += 50;
        cycle(&c, t, far);
    }
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
}

static void test_failure_timing_and_events(void)
{
    tof_core_t c = started();
    tof_view_t v;
    tof_events_t ev;
    uint32_t t = 0;
    free_all(&c, &t, 3);                       // t = 150, останній відлік на 150
    uint16_t only_rear[TOF_SENSORS] = {0xFFFF, 0xFFFF, 0xFFFF, 1000};
    t = 150 + 249;
    ev = cycle(&c, t, only_rear);
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state != TOF_S_FAILED);   // 249 мс — ще справний
    CHECK(ev.failed_now == 0);
    t = 150 + 250;
    ev = cycle(&c, t, only_rear);
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state == TOF_S_FAILED);
    CHECK(ev.failed_now == (uint8_t)((1u << TOF_LEFT_FRONT) | (1u << TOF_FRONT) | (1u << TOF_RIGHT_FRONT)));
    t += 50;
    ev = cycle(&c, t, only_rear);
    CHECK(ev.failed_now == 0);                          // одне повідомлення на зміну
    CHECK(v.sensor[TOF_REAR].state != TOF_S_FAILED);
}

static void test_failed_sensor_excluded_from_sector(void)
{
    tof_core_t c = started();
    tof_view_t v;
    tof_events_t ev = {0};
    uint32_t t = 0;
    free_all(&c, &t, 3);
    tof_core_mark_failed(&c, TOF_FRONT, &ev);
    CHECK(ev.failed_now == (1u << TOF_FRONT));
    uint16_t m[TOF_SENSORS] = {400, 50, 300, 1000};      // відмовивший U8 «бачить» 50, але не враховується
    for (int i = 0; i < 3; i++) {
        t += 50;
        cycle(&c, t, m);
    }
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state == TOF_S_FAILED);
    CHECK(v.sector[TOF_SECTOR_FRONT].mm == 300);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);
}

static void test_sector_without_working_sensors(void)
{
    tof_core_t c = started();
    tof_view_t v;
    tof_events_t ev = {0};
    uint32_t t = 0;
    uint16_t near_[TOF_SENSORS] = {100, 100, 100, 100};
    for (int i = 0; i < 4; i++) {
        t += 50;
        cycle(&c, t, near_);
    }
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    tof_core_mark_failed(&c, TOF_LEFT_FRONT, &ev);
    tof_core_mark_failed(&c, TOF_FRONT, &ev);
    tof_core_mark_failed(&c, TOF_RIGHT_FRONT, &ev);
    t += 50;
    uint16_t none[TOF_SENSORS] = {0xFFFF, 0xFFFF, 0xFFFF, 100};
    cycle(&c, t, none);
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].guarded);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);          // сектор без справних датчиків вільний
    CHECK(v.sector[TOF_SECTOR_REAR].occupied);
    CHECK(v.protection);
    tof_core_mark_failed(&c, TOF_REAR, &ev);
    tof_core_tick(&c, t + 50, &ev);
    tof_core_view(&c, &v);
    CHECK(!v.protection);
}

static void test_restore_and_resume(void)
{
    tof_core_t c = started();
    tof_view_t v;
    tof_events_t ev = {0};
    uint32_t t = 0;
    free_all(&c, &t, 3);
    tof_core_mark_failed(&c, TOF_REAR, &ev);
    tof_core_start(&c, TOF_REAR, t, &ev);
    CHECK(ev.restored_now == (1u << TOF_REAR));
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_REAR].state == TOF_S_NOT_READY);
    // пауза опитування довша за 250 мс не дає хибної відмови після resume
    uint32_t t_pause = t + 3000;
    tof_core_resume(&c, t_pause);
    uint16_t far[TOF_SENSORS] = {1000, 1000, 1000, 1000};
    ev.failed_now = 0;
    ev = cycle(&c, t_pause + 50, far);
    CHECK(ev.failed_now == 0);
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state != TOF_S_FAILED);
}

static void test_init_state_not_failed(void)
{
    tof_core_t c;
    tof_events_t ev = {0};
    tof_view_t v;
    tof_core_init(&c);
    tof_core_tick(&c, 100000, &ev);
    CHECK(ev.failed_now == 0);
    tof_core_view(&c, &v);
    CHECK(v.sensor[TOF_FRONT].state == TOF_S_INIT);
    CHECK(!v.protection);
}

int main(void)
{
    test_constants();
    test_not_ready_before_three_samples();
    test_out_of_range_is_valid();
    test_threshold_strict();
    test_spike_ignored_and_two_of_three();
    test_release_after_three_updates();
    test_release_counter_resets();
    test_failure_timing_and_events();
    test_failed_sensor_excluded_from_sector();
    test_sector_without_working_sensors();
    test_restore_and_resume();
    test_init_state_not_failed();
    return test_summary();
}
