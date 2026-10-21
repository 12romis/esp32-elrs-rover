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

// ───────── T2: обмеження тяги і тексти ─────────
static float absf(float x)
{
    return x < 0 ? -x : x;
}

static float maxf(float a, float b)
{
    return a > b ? a : b;
}

static bool near_f(float a, float b)
{
    return absf(a - b) < 1e-5f;
}

static void test_limit_front(void)
{
    float l, r;
    tof_limit_sides(0.5f, 0.5f, true, false, &l, &r);
    CHECK(near_f(l, 0) && near_f(r, 0));                    // уперед заборонено
    tof_limit_sides(0.3f, -0.3f, true, false, &l, &r);
    CHECK(near_f(l, 0.3f) && near_f(r, -0.3f));             // чистий поворот не чіпаємо
    tof_limit_sides(1.0f, 0.2f, true, false, &l, &r);
    CHECK(near_f(l, 0.4f) && near_f(r, -0.4f));             // тяга 0.6 → 0, поворот 0.4 лишається
    tof_limit_sides(-0.5f, -0.5f, true, false, &l, &r);
    CHECK(near_f(l, -0.5f) && near_f(r, -0.5f));            // назад дозволено, коли зайнятий лише передній
}

static void test_limit_rear(void)
{
    float l, r;
    tof_limit_sides(-0.5f, -0.5f, false, true, &l, &r);
    CHECK(near_f(l, 0) && near_f(r, 0));
    tof_limit_sides(0.5f, 0.5f, false, true, &l, &r);
    CHECK(near_f(l, 0.5f) && near_f(r, 0.5f));              // уперед дозволено
    tof_limit_sides(-1.0f, -0.2f, false, true, &l, &r);
    CHECK(near_f(l, -0.4f) && near_f(r, 0.4f));
}

static void test_limit_both_and_none(void)
{
    float l, r;
    tof_limit_sides(0.5f, 0.5f, true, true, &l, &r);
    CHECK(near_f(l, 0) && near_f(r, 0));
    tof_limit_sides(-0.5f, -0.5f, true, true, &l, &r);
    CHECK(near_f(l, 0) && near_f(r, 0));
    tof_limit_sides(0.4f, -0.4f, true, true, &l, &r);
    CHECK(near_f(l, 0.4f) && near_f(r, -0.4f));             // поворот на місці працює
    tof_limit_sides(0.7f, 0.1f, false, false, &l, &r);
    CHECK(l == 0.7f && r == 0.1f);                          // жоден не зайнятий: без змін, точно
}

static void test_limit_never_exceeds(void)
{
    for (int a = -10; a <= 10; a++) {
        for (int b = -10; b <= 10; b++) {
            for (int m = 0; m < 4; m++) {
                float l = a / 10.0f, r = b / 10.0f, lo, ro;
                tof_limit_sides(l, r, m & 1, m & 2, &lo, &ro);
                float in_max = maxf(absf(l), absf(r)), out_max = maxf(absf(lo), absf(ro));
                CHECK(out_max <= in_max + 1e-5f);           // найбільший модуль борту не зростає, ≤ 1
                CHECK(out_max <= 1.0f + 1e-5f);
                if (m & 1) CHECK((lo + ro) / 2 <= 1e-5f || (l + r) / 2 <= 0);   // тяга вперед обрізана
                if (m & 2) CHECK((lo + ro) / 2 >= -1e-5f || (l + r) / 2 >= 0);  // тяга назад обрізана
                CHECK(near_f((lo - ro) / 2, (l - r) / 2));  // поворот не змінюється
            }
        }
    }
}

static void test_show_text(void)
{
    tof_core_t c = started();
    tof_events_t ev = {0};
    tof_view_t v;
    char buf[600];
    uint32_t t = 0;
    uint16_t m[TOF_SENSORS] = {412, 150, 8190, 0xFFFF};
    for (int i = 0; i < 3; i++) {
        t += 50;
        cycle(&c, t, m);
    }
    tof_core_mark_failed(&c, TOF_REAR, &ev);
    tof_core_view(&c, &v);
    v.valid = true;
    size_t n = tof_fmt_show(buf, sizeof(buf), &v, false);
    CHECK(n > 0 && n < sizeof(buf) && n == strlen(buf));
    CHECK(strstr(buf, "170") != NULL);                       // поріг
    CHECK(strstr(buf, "412") != NULL);
    CHECK(strstr(buf, "150") != NULL);
    CHECK(strstr(buf, "поза діапазоном") != NULL);
    CHECK(strstr(buf, "відмова") != NULL);
    CHECK(strstr(buf, "зайнятий") != NULL);                  // передній сектор: 150 < 170
    CHECK(strstr(buf, "не охороняється") != NULL);           // задній без справних датчиків
    CHECK(strstr(buf, "8190") == NULL);                      // сире число «поза діапазоном» не друкується
}

static void test_show_text_edge(void)
{
    tof_view_t v;
    char buf[600];
    memset(&v, 0, sizeof(v));
    size_t n = tof_fmt_show(buf, sizeof(buf), &v, false);    // знімок не отримано
    CHECK(n > 0 && strstr(buf, "ініціалізація") != NULL);
    v.valid = true;
    n = tof_fmt_show(buf, sizeof(buf), &v, true);            // застарілий
    CHECK(strstr(buf, "застарів") != NULL);
    char tiny[16];
    n = tof_fmt_show(tiny, sizeof(tiny), &v, false);         // малий буфер: обрізано, завершено нулем
    CHECK(n < sizeof(tiny) && strlen(tiny) == n);
    CHECK(tof_fmt_show(NULL, 0, &v, false) == 0);
}

static void test_hold_line(void)
{
    char buf[320];
    size_t n = tof_fmt_hold_line(buf, sizeof(buf), TOF_SECTOR_FRONT, 150, TOF_FRONT, true, 1000, 1096);
    CHECK(n > 0 && n == strlen(buf));
    CHECK(strstr(buf, "перед") != NULL);                     // «передній»
    CHECK(strstr(buf, "150") != NULL);
    CHECK(strstr(buf, "170") != NULL);                       // поріг
    CHECK(strstr(buf, "U8") != NULL);
    CHECK(strstr(buf, "1000") != NULL && strstr(buf, "1096") != NULL && strstr(buf, "різниця 96 мс") != NULL);   // мітки й затримка (review R3)
    tof_fmt_hold_line(buf, sizeof(buf), TOF_SECTOR_FRONT, 150, TOF_FRONT, true, 0xFFFFFFF0u, 0x50u);   // переповнення годинника
    CHECK(strstr(buf, "різниця 96 мс") != NULL);
    n = tof_fmt_hold_line(buf, sizeof(buf), TOF_SECTOR_REAR, 90, TOF_REAR, false, 0, 2000);
    CHECK(strstr(buf, "перший відлік") == NULL);                // міток немає — рядок без них
    CHECK(strstr(buf, "задн") != NULL && strstr(buf, "U10") != NULL);
    CHECK(strcmp(tof_sensor_name(TOF_LEFT_FRONT), "U7") == 0);
    CHECK(strcmp(tof_sensor_name(TOF_RIGHT_FRONT), "U9") == 0);
}

// ───────── review R3: мітка першого сирого відліку менше порога (AC-01, «≤ 250 мс») ─────────
static void test_first_below_timestamp(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint32_t t = 0;
    free_all(&c, &t, 3);                                     // t = 150
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].has_below);
    uint16_t low[TOF_SENSORS] = {1000, 50, 1000, 1000};
    t = 200;
    cycle(&c, t, low);                                       // перший відлік < порога; медіана ще висока
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(v.sector[TOF_SECTOR_FRONT].has_below && v.sector[TOF_SECTOR_FRONT].below_ms == 200);
    t = 250;
    cycle(&c, t, low);                                       // другий — медіана низька, сектор зайнятий
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].occupied);
    CHECK(v.sector[TOF_SECTOR_FRONT].has_below && v.sector[TOF_SECTOR_FRONT].below_ms == 200);   // мітка першого, не другого
    CHECK(!v.sector[TOF_SECTOR_REAR].has_below);
    // звільнення: мітка скидається, коли у кільцях більше немає відліків нижче порога
    for (int i = 0; i < 6; i++) {
        t += 50;
        uint16_t far[TOF_SENSORS] = {1000, 1000, 1000, 1000};
        cycle(&c, t, far);
    }
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied && !v.sector[TOF_SECTOR_FRONT].has_below);
}

static void test_single_spike_clears_below(void)
{
    tof_core_t c = started();
    tof_view_t v;
    uint32_t t = 0;
    free_all(&c, &t, 3);
    uint16_t low[TOF_SENSORS] = {1000, 50, 1000, 1000}, far[TOF_SENSORS] = {1000, 1000, 1000, 1000};
    t += 50;
    cycle(&c, t, low);
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].has_below);
    for (int i = 0; i < 3; i++) {                            // викид витіснено з кільця — мітки немає
        t += 50;
        cycle(&c, t, far);
    }
    tof_core_view(&c, &v);
    CHECK(!v.sector[TOF_SECTOR_FRONT].occupied && !v.sector[TOF_SECTOR_FRONT].has_below);
    // нова подія отримує нову мітку
    t += 50;
    cycle(&c, t, low);
    tof_core_view(&c, &v);
    CHECK(v.sector[TOF_SECTOR_FRONT].has_below && v.sector[TOF_SECTOR_FRONT].below_ms == t);
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
    test_limit_front();
    test_limit_rear();
    test_limit_both_and_none();
    test_limit_never_exceeds();
    test_show_text();
    test_show_text_edge();
    test_hold_line();
    test_first_below_timestamp();
    test_single_spike_clears_below();
    return test_summary();
}
