// Хостові тести battery_core (spec AC-13, AC-14, AC-22): перерахунок у мВ, поріг, свіжість знімка.
#include <stdint.h>

#include "battery_calib.h"
#include "battery_core.h"
#include "test_support.h"

static void test_avg(void)
{
    uint16_t s16[16];
    for (int i = 0; i < 16; i++) s16[i] = (uint16_t)(2000 + i);  // 2000..2015
    CHECK(battery_avg_mv(s16, 16) == 2007);  // (2000+2015)/2 = 2007.5 → ціле ділення вниз
    CHECK(battery_avg_mv(s16, 0) == 0);
    CHECK(battery_avg_mv(NULL, 16) == 0);
}

static void test_adc_to_mv(void)
{
    // Двоточкове калібрування (P3, docs/measured.md, 2026-10-18): k = 3.973, зсув +151 мВ.
    CHECK(BATTERY_SCALE_PPM == 3973 && BATTERY_OFFSET_MV == 151);
    CHECK(battery_adc_to_mv(0) == 151);
    CHECK(battery_adc_to_mv(1000) == 4124);
    // Виміряні точки: 1484 мВ на піні ↔ 6047 мВ на мультиметрі; ≈ 2041 мВ ↔ 8260 мВ на вольтметрі (похибка ≤ 1 мВ).
    CHECK(battery_adc_to_mv(1484) == 6046);
    CHECK(battery_adc_to_mv(2041) == 8259);
    // Межа відсічки 6000 мВ: 1472 мВ на піні — ще нижче порога, 1473 — вже ні.
    CHECK(battery_is_below_cutoff(battery_adc_to_mv(1472)));
    CHECK(!battery_is_below_cutoff(battery_adc_to_mv(1473)));
}

static void test_cutoff(void)
{
    CHECK(BATTERY_CUTOFF_MV == 6000);
    CHECK(battery_is_below_cutoff(5999));
    CHECK(!battery_is_below_cutoff(6000));
    CHECK(!battery_is_below_cutoff(6001));
    CHECK(battery_is_below_cutoff(0));  // батареї немає: напруга близька до нуля
}

static void test_snapshot(void)
{
    battery_snapshot_t invalid = {.mv = 8000, .t_ms = 1000, .valid = false};
    battery_snapshot_t s = {.mv = 8000, .t_ms = 1000, .valid = true};
    CHECK(!battery_snapshot_usable(&invalid, 1000));
    CHECK(battery_snapshot_usable(&s, 1000));
    CHECK(battery_snapshot_usable(&s, 1600));   // рівно 600 мс — ще свіжий
    CHECK(!battery_snapshot_usable(&s, 1601));  // 601 мс — застарілий
    CHECK(!battery_snapshot_usable(NULL, 1000));
    // знімок «з майбутнього» (battery записала його після того, як control взяв now): свіжий, не застарілий
    CHECK(battery_snapshot_usable(&s, 999));
    CHECK(battery_snapshot_usable(&s, 990));
    // Перехід лічильника мс через нуль: t_ms біля максимуму, now після обороту
    battery_snapshot_t w = {.mv = 8000, .t_ms = 0xFFFFFF00u, .valid = true};
    CHECK(battery_snapshot_usable(&w, 0x00000050u));   // 336 мс по модулю 2^32
    CHECK(!battery_snapshot_usable(&w, 0x00000300u));  // 1024 мс
}

int main(void)
{
    test_avg();
    test_adc_to_mv();
    test_cutoff();
    test_snapshot();
    return test_summary();
}
