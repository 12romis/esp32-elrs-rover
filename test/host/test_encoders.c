// Хостові тести encoders_core (spec AC-04, AC-09, AC-10; ADR-0002): приріст за модулем, знак колеса, накопичувач, обнулення.
#include <stdint.h>
#include <string.h>

#include "encoders_calib.h"
#include "encoders_core.h"
#include "test_support.h"

static const int SIGN[ENC_WHEELS] = ENC_DIR_SIGN;

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
    enc_core_sample(&s, raw);
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
    enc_core_sample(&s, raw);
    for (int64_t n = 50; n <= 27720; n += 50) {
        forward_raw(n, raw);
        enc_core_sample(&s, raw);
    }
    forward_raw(27720, raw);   // крок 50 закінчується на 27700 — довести рівно до 10 обертів
    enc_core_sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 27720);
    // Назад до нуля.
    for (int64_t n = 27720 - 70; n >= 0; n -= 70) {
        forward_raw(n, raw);
        enc_core_sample(&s, raw);
    }
    forward_raw(0, raw);
    enc_core_sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 0);
}

static void test_many_wraps_monotonic(void)
{
    // 100 000+ імпульсів (понад три межі) кроками по 400: лічильник лише зростає, без стрибків.
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw);
    int64_t prev = 0;
    int jumps = 0;
    for (int64_t n = 400; n <= 120000; n += 400) {
        forward_raw(n, raw);
        enc_core_sample(&s, raw);
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
        enc_core_sample(&s, raw);
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
    enc_core_sample(&s, raw);
    for (int k = 1; k <= 20; k++) {
        forward_raw((int64_t)k * ENC_STEP_MAX, raw);
        enc_core_sample(&s, raw);
    }
    CHECK(enc_core_count(&s, ENC_FL) == 20LL * ENC_STEP_MAX);
}

static void test_accum_near_limit(void)
{
    // Накопичувач біля 10^15 не переповнюється (spec §6 «Діапазон накопичення»).
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS] = { 0, 0, 0, 0 };
    enc_core_sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) {
        s.w[i].accum = (i % 2 == 0) ? 1000000000000000LL : -1000000000000000LL;
        s.w[i].last_raw = 0;
    }
    forward_raw(1000, raw);
    enc_core_sample(&s, raw);
    CHECK(enc_core_count(&s, ENC_FL) == 1000000000001000LL);
    CHECK(enc_core_count(&s, ENC_FR) == -999999999999000LL);
}

static void test_reset_offset(void)
{
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw);
    forward_raw(5000, raw);
    enc_core_sample(&s, raw);
    int64_t acc = s.w[ENC_FL].accum;
    enc_core_reset(&s);
    CHECK(s.w[ENC_FL].accum == acc);   // накопичувач не змінюється
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 0);
    forward_raw(5300, raw);
    enc_core_sample(&s, raw);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(enc_core_count(&s, (enc_wheel_t)i) == 300);
}

static void test_unavailable_skipped(void)
{
    enc_state_t s;
    enc_core_init(&s);
    s.w[ENC_FR].available = false;
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw);
    forward_raw(900, raw);
    enc_core_sample(&s, raw);
    CHECK(enc_core_count(&s, ENC_FR) == 0);
    CHECK(enc_core_count(&s, ENC_FL) == 900);
    // Обнулення не чіпає недоступне колесо.
    s.w[ENC_FR].accum = 7;
    enc_core_reset(&s);
    CHECK(s.w[ENC_FR].offset == 0);
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
    return test_summary();
}
