// Хостові тести encoders_core (spec AC-02, AC-04, AC-09, AC-10; ADR-0002): приріст за модулем, знак колеса, накопичувач,
// обнулення, швидкість як приріст на фактичний час, лічильник інтервалів.
#include <stdint.h>
#include <string.h>

#include "encoders_calib.h"
#include "encoders_core.h"
#include "test_support.h"

static const int SIGN[ENC_WHEELS] = ENC_DIR_SIGN;

// Підставний годинник: кожна вибірка T1-тестів — рівно 20 мс після попередньої.
static uint64_t g_now_us;
static void sample(enc_state_t *s, const int32_t raw[ENC_WHEELS])
{
    g_now_us += 20000;
    enc_core_sample(s, raw, g_now_us);
}

// Імітація апаратного лічильника: рахує від нуля і скидається в нуль на ±ENC_HW_LIMIT.
static int32_t hw_wrap(int64_t v)
{
    return (int32_t)(v % ENC_HW_LIMIT);
}

// Сирі значення чотирьох коліс, що відповідають руху «вперед» на n імпульсів від нуля.
static void forward_raw(int64_t n, int32_t raw[ENC_WHEELS])
{
    for (int i = 0; i < ENC_WHEELS; i++) raw[i] = hw_wrap(SIGN[i] * n);
}

static void test_delta(void)
{
    CHECK(enc_core_delta(0, 0) == 0);
    CHECK(enc_core_delta(100, 250) == 150);
    CHECK(enc_core_delta(250, 100) == -150);
    // Перехід через межу вгору: 29990 → (30000 → 0) → 10 = +20.
    CHECK(enc_core_delta(29990, 10) == 20);
    // Через межу вниз: −29990 → 0 → −10 = −20.
    CHECK(enc_core_delta(-29990, -10) == -20);
    // Від'ємне сире значення ≡ додатному за модулем: −1 ≡ 29999.
    CHECK(enc_core_delta(29999, -1) == 0);
    CHECK(enc_core_delta(-1, 0) == 1);
    // Межі однієї вибірки: 14999 і 15000 вперед розрізняються, −15000 зводиться до +15000.
    CHECK(enc_core_delta(0, 14999) == 14999);
    CHECK(enc_core_delta(0, 15000) == 15000);
    CHECK(enc_core_delta(0, -15000) == 15000);
    CHECK(enc_core_delta(0, -14999) == -14999);
    CHECK(enc_core_delta(0, 15001) == -14999);
}

static void test_first_sample_primes(void)
{
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS] = { 500, -700, 1200, 3 };
    sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 0);
    CHECK(s.primed);
}

static void test_sign_forward_positive(void)
{
    // Рух уперед на 27720 імпульсів (10 обертів) дає +27720 на кожному колесі, хоч сирий знак різний.
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    sample(&s, raw);
    for (int64_t n = 50; n <= 27720; n += 50) {
        forward_raw(n, raw);
        sample(&s, raw);
    }
    forward_raw(27720, raw);   // крок 50 закінчується на 27700 — довести рівно до 10 обертів
    sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 27720);
    // Назад до нуля.
    for (int64_t n = 27720 - 70; n >= 0; n -= 70) {
        forward_raw(n, raw);
        sample(&s, raw);
    }
    forward_raw(0, raw);
    sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 0);
}

static void test_many_wraps_monotonic(void)
{
    // 100 000+ імпульсів (понад три межі) кроками по 400: лічильник лише зростає, без стрибків.
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    sample(&s, raw);
    int64_t prev = 0;
    int jumps = 0;
    for (int64_t n = 400; n <= 120000; n += 400) {
        forward_raw(n, raw);
        sample(&s, raw);
        int64_t c = enc_core_count(&s, ENC_FR);
        if (c - prev != 400) jumps++;
        prev = c;
    }
    CHECK(jumps == 0);
    CHECK(enc_core_count(&s, ENC_FL) == 120000);
    CHECK(enc_core_count(&s, ENC_RR) == 120000);
    // Назад так само: лише спадає.
    jumps = 0;
    for (int64_t n = 120000 - 333; n >= -120000; n -= 333) {
        forward_raw(n, raw);
        sample(&s, raw);
        int64_t c = enc_core_count(&s, ENC_RL);
        if (prev - c != 333) jumps++;
        prev = c;
    }
    CHECK(jumps == 0);
}

static void test_max_step_each_sample(void)
{
    // Рух на ENC_STEP_MAX за вибірку ще читається правильно через багато меж.
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    sample(&s, raw);
    for (int k = 1; k <= 20; k++) {
        forward_raw((int64_t)k * ENC_STEP_MAX, raw);
        sample(&s, raw);
    }
    CHECK(enc_core_count(&s, ENC_FL) == 20LL * ENC_STEP_MAX);
}

static void test_accum_near_limit(void)
{
    // Накопичувач біля 10^15 не переповнюється (spec §6 «Діапазон накопичення»).
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS] = { 0, 0, 0, 0 };
    sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) {
        s.w[i].accum = (i % 2 == 0) ? 1000000000000000LL : -1000000000000000LL;
        s.w[i].last_raw = 0;
    }
    forward_raw(1000, raw);
    sample(&s, raw);
    CHECK(enc_core_count(&s, ENC_FL) == 1000000000001000LL);
    CHECK(enc_core_count(&s, ENC_FR) == -999999999999000LL);
}

static void test_reset_offset(void)
{
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    sample(&s, raw);
    forward_raw(5000, raw);
    sample(&s, raw);
    int64_t acc = s.w[ENC_FL].accum;
    enc_core_reset(&s);
    CHECK(s.w[ENC_FL].accum == acc);   // накопичувач не змінюється
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 0);
    forward_raw(5300, raw);
    sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 300);
}

static void test_unavailable_skipped(void)
{
    enc_state_t s;
    enc_core_init(&s);
    s.w[ENC_FR].available = false;
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    sample(&s, raw);
    forward_raw(900, raw);
    sample(&s, raw);
    CHECK(enc_core_count(&s, ENC_FR) == 0);
    CHECK(enc_core_count(&s, ENC_FL) == 900);
    // Обнулення не чіпає недоступне колесо.
    s.w[ENC_FR].accum = 7;
    enc_core_reset(&s);
    CHECK(s.w[ENC_FR].offset == 0);
}


static void test_speed_actual_time(void)
{
    // 100 імпульсів за 20 мс = 5000 імп/с; ті самі 100 імпульсів за 25 мс = 4000 імп/с (ділення на фактичний час).
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, 1000000);
    CHECK(s.intervals == 0);
    forward_raw(100, raw);
    enc_core_sample(&s, raw, 1020000);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(s.w[i].speed_pps > 4999.9f && s.w[i].speed_pps < 5000.1f);
    forward_raw(200, raw);
    enc_core_sample(&s, raw, 1045000);
    CHECK(s.w[ENC_FR].speed_pps > 3999.9f && s.w[ENC_FR].speed_pps < 4000.1f);
    CHECK(s.intervals == 2);
    // Однаковий рух при тремтінні такту 19 / 21 мс дає однакову швидкість на одиницю часу.
    forward_raw(200 + 190, raw);
    enc_core_sample(&s, raw, 1064000);
    CHECK(s.w[ENC_FL].speed_pps > 9999.9f && s.w[ENC_FL].speed_pps < 10000.1f);
    forward_raw(200 + 190 + 210, raw);
    enc_core_sample(&s, raw, 1085000);
    CHECK(s.w[ENC_FL].speed_pps > 9999.9f && s.w[ENC_FL].speed_pps < 10000.1f);
}

static void test_speed_zero_after_stop(void)
{
    // Порожній інтервал одразу дає нуль: не пізніше другої вибірки після останнього імпульсу (≤ 40 мс).
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, 0);
    forward_raw(300, raw);
    enc_core_sample(&s, raw, 20000);
    CHECK(s.w[ENC_RL].speed_pps > 0.0f);
    // Останній імпульс одразу після вибірки 20 мс: наступна вибірка ще бачить рух, через одну — нуль.
    forward_raw(301, raw);
    enc_core_sample(&s, raw, 40000);
    CHECK(s.w[ENC_RL].speed_pps > 0.0f);
    enc_core_sample(&s, raw, 60000);
    CHECK(s.w[ENC_RL].speed_pps == 0.0f);
    CHECK(enc_core_count(&s, ENC_RL) == 301);
}

static void test_speed_backward_negative(void)
{
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(1000, raw);
    enc_core_sample(&s, raw, 0);
    forward_raw(900, raw);
    enc_core_sample(&s, raw, 20000);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(s.w[i].speed_pps < -4999.9f && s.w[i].speed_pps > -5000.1f);
}

static void test_speed_same_timestamp(void)
{
    // Дві вибірки з однаковою міткою: без ділення на нуль, інтервал не рахується, рух накопичується.
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, 5000);
    forward_raw(100, raw);
    enc_core_sample(&s, raw, 25000);
    float before = s.w[ENC_FL].speed_pps;
    forward_raw(150, raw);
    enc_core_sample(&s, raw, 25000);
    CHECK(s.intervals == 1);
    CHECK(s.w[ENC_FL].speed_pps == before);
    CHECK(enc_core_count(&s, ENC_FL) == 150);
}

static void test_speed_timestamp_wrap(void)
{
    // Мітка в мкс біля переповнення 64 біт: беззнакова різниця дає правильний проміжок.
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, UINT64_MAX - 9999);
    forward_raw(100, raw);
    enc_core_sample(&s, raw, 10000);
    CHECK(s.w[ENC_FL].speed_pps > 4999.9f && s.w[ENC_FL].speed_pps < 5000.1f);
}

static void test_reset_keeps_speed(void)
{
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, 0);
    forward_raw(100, raw);
    enc_core_sample(&s, raw, 20000);
    enc_core_reset(&s);
    forward_raw(200, raw);
    enc_core_sample(&s, raw, 40000);
    CHECK(s.w[ENC_FL].speed_pps > 4999.9f && s.w[ENC_FL].speed_pps < 5000.1f);  // без стрибка після обнулення
}

static void test_interval_count_10s(void)
{
    // 10 с вибірок по 20 мс → 500 інтервалів (spec §6).
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS] = { 0, 0, 0, 0 };
    enc_core_sample(&s, raw, 0);
    for (uint64_t t = 20000; t <= 10000000; t += 20000) enc_core_sample(&s, raw, t);
    CHECK(s.intervals == 500);
}

int main(void)
{
    test_delta();
    test_first_sample_primes();
    test_sign_forward_positive();
    test_many_wraps_monotonic();
    test_max_step_each_sample();
    test_accum_near_limit();
    test_reset_offset();
    test_unavailable_skipped();
    test_speed_actual_time();
    test_speed_zero_after_stop();
    test_speed_backward_negative();
    test_speed_same_timestamp();
    test_speed_timestamp_wrap();
    test_reset_keeps_speed();
    test_interval_count_10s();
    return test_summary();
}
