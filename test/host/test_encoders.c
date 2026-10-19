// Хостові тести encoders_core (spec AC-02, AC-04, AC-09, AC-10; ADR-0002): приріст за модулем, знак колеса, накопичувач,
// обнулення, швидкість як приріст на фактичний час, лічильник інтервалів; одиниці, доступність коліс, тексти
// знімка, enc reset і status (AC-01, AC-06, AC-08).
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

static void test_speed_catchup_burst(void)
{
    // Review F1: після паузи задачі vTaskDelayUntil наздоганяє — вибірки через десятки мкс. Вони накопичують рух,
    // але інтервалом не є: швидкість без викиду, рух коротких вибірок входить у наступний інтервал.
    CHECK(ENC_PERIOD_US == 20000);
    CHECK(ENC_MIN_INTERVAL_US == ENC_PERIOD_US / 2);
    enc_state_t s;
    enc_core_init(&s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, 0);
    forward_raw(600, raw);
    enc_core_sample(&s, raw, 60000);   // пауза 60 мс: 600 імпульсів за 60 мс = 10000 імп/с
    CHECK(s.w[ENC_FL].speed_pps > 9999.9f && s.w[ENC_FL].speed_pps < 10000.1f);
    CHECK(s.intervals == 1);
    forward_raw(601, raw);
    enc_core_sample(&s, raw, 60050);   // наздоганяння: 1 імпульс за 50 мкс дав би 20000 імп/с
    CHECK(s.w[ENC_FL].speed_pps > 9999.9f && s.w[ENC_FL].speed_pps < 10000.1f);
    enc_core_sample(&s, raw, 60100);   // наздоганяння без руху дав би хибний нуль
    CHECK(s.w[ENC_FL].speed_pps > 9999.9f && s.w[ENC_FL].speed_pps < 10000.1f);
    CHECK(s.intervals == 1);
    CHECK(s.last_us == 60000);
    CHECK(enc_core_count(&s, ENC_FL) == 601);
    // Наступна вибірка за 20 мс від мітки: 1 імпульс коротких вибірок + 199 нових = 200 за 20 мс = 10000 імп/с.
    forward_raw(800, raw);
    enc_core_sample(&s, raw, 80000);
    for (int i = 0; i < ENC_WHEELS; i++) CHECK(s.w[i].speed_pps > 9999.9f && s.w[i].speed_pps < 10000.1f);
    CHECK(s.intervals == 2);
    // Межа: рівно ENC_MIN_INTERVAL_US — уже інтервал, на 1 мкс менше — ще ні.
    forward_raw(900, raw);
    enc_core_sample(&s, raw, 80000 + ENC_MIN_INTERVAL_US - 1);
    CHECK(s.intervals == 2);
    enc_core_sample(&s, raw, 80000 + ENC_MIN_INTERVAL_US);
    CHECK(s.intervals == 3);
    CHECK(s.w[ENC_RR].speed_pps > 9999.9f && s.w[ENC_RR].speed_pps < 10000.1f);   // 100 імп за 10 мс
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

// Стан після руху вперед на n імпульсів за останні 20 мс (швидкість n / 20 мс).
static void moved(enc_state_t *s, int64_t n)
{
    enc_core_init(s);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(s, raw, 1000000);
    forward_raw(n, raw);
    enc_core_sample(s, raw, 1020000);
}

static void test_units(void)
{
    CHECK(enc_core_revs(27720) > 9.9999 && enc_core_revs(27720) < 10.0001);
    // Довжина кола π × 44 мм: 10 обертів ≈ 138.23 см.
    CHECK(enc_core_cm(27720) > 138.22 && enc_core_cm(27720) < 138.24);
    // 2772 імп/с = 1 оберт/с = 60 об/хв ≈ 13.82 см/с.
    CHECK(enc_core_rpm(2772.0f) > 59.999 && enc_core_rpm(2772.0f) < 60.001);
    CHECK(enc_core_cm_s(2772.0f) > 13.82 && enc_core_cm_s(2772.0f) < 13.83);
    CHECK(enc_core_cm(-27720) < -138.22);
}

static void test_snapshot_format(void)
{
    enc_state_t s;
    moved(&s, 27720 % ENC_STEP_MAX);   // 12720 імпульсів за 20 мс
    char buf[512];
    size_t n = enc_core_format_snapshot(&s, buf, sizeof(buf));
    CHECK(n == strlen(buf));
    CHECK(strstr(buf, "1020 мс") != NULL);          // мітка вибірки в мс від старту
    CHECK(strstr(buf, "інтервалів 1") != NULL);
    CHECK(strstr(buf, "FL: 12720 імп, 4.59 об") != NULL);
    // Порядок коліс FL, FR, RL, RR.
    const char *fl = strstr(buf, "FL:"), *fr = strstr(buf, "FR:"), *rl = strstr(buf, "RL:"), *rr = strstr(buf, "RR:");
    CHECK(fl && fr && rl && rr && fl < fr && fr < rl && rl < rr);
    // 12720 імп за 20 мс = 636000 імп/с → 13766.2 об/хв; 4.59 об → 63.4 см; 636000 імп/с → 3171.5 см/с.
    CHECK(strstr(buf, "13766.2 об/хв") != NULL);
    CHECK(strstr(buf, "63.4 см,") != NULL);
    CHECK(strstr(buf, "3171.5 см/с") != NULL);
}

static void test_snapshot_negative(void)
{
    enc_state_t s;
    moved(&s, -2772);
    char buf[512];
    enc_core_format_snapshot(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "RR: -2772 імп, -1.00 об") != NULL);
    CHECK(strstr(buf, "-13.8 см,") != NULL);
}

static void test_snapshot_before_first_sample(void)
{
    enc_state_t s;
    enc_core_init(&s);
    char buf[512];
    enc_core_format_snapshot(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "вибірок ще не було") != NULL);
}

static void test_unavailable_wheel_in_snapshot(void)
{
    enc_state_t s;
    enc_core_init(&s);
    enc_core_set_unavailable(&s, ENC_FR, "ESP_ERR_NOT_FOUND");
    CHECK(enc_core_available_count(&s) == 3);
    int32_t raw[ENC_WHEELS];
    forward_raw(0, raw);
    enc_core_sample(&s, raw, 0);
    forward_raw(500, raw);
    enc_core_sample(&s, raw, 20000);
    char buf[512];
    enc_core_format_snapshot(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "FR: недоступне — ESP_ERR_NOT_FOUND") != NULL);
    const char *fr = strstr(buf, "FR:");
    const char *eol = fr ? strchr(fr, '\n') : NULL;
    CHECK(fr && eol && memchr(fr, '0', (size_t)(eol - fr)) == NULL);   // жодних нулів, що виглядають як виміряні
    CHECK(strstr(buf, "FL: 500 імп") != NULL);
    CHECK(strstr(buf, "RR: 500 імп") != NULL);
}

static void test_reason_not_copied(void)
{
    // Review F5: причина — вказівник на рядок статичного часу життя, а не копія в стані (знімок < 200 байтів, sad.md §4).
    static const char why[] = "ESP_ERR_TIMEOUT";
    enc_state_t s;
    enc_core_init(&s);
    CHECK(sizeof(s.w[0].reason) == sizeof(const char *));
    enc_core_set_unavailable(&s, ENC_RL, why);
    CHECK(s.w[ENC_RL].reason == why);
    enc_core_set_unavailable(&s, ENC_RR, NULL);
    char buf[512];
    enc_core_format_snapshot(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "RL: недоступне — ESP_ERR_TIMEOUT") != NULL);
    CHECK(strstr(buf, "RR: недоступне — невідома причина") != NULL);
}

static void test_all_unavailable(void)
{
    enc_state_t s;
    enc_core_init(&s);
    for (int i = 0; i < ENC_WHEELS; i++) enc_core_set_unavailable(&s, (enc_wheel_t)i, "задача encoders не запущена");
    char buf[512];
    enc_core_format_snapshot(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "RL: недоступне — задача encoders не запущена") != NULL);
    enc_core_reset(&s);
    enc_core_format_reset(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "обнуляти нічого") != NULL);
    CHECK(strstr(buf, "FL, FR, RL, RR") != NULL);
    enc_core_format_status(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "0 з 4 доступні") != NULL);
}

static void test_reset_and_status_texts(void)
{
    enc_state_t s;
    enc_core_init(&s);
    char buf[256];
    enc_core_format_reset(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "обнулено") != NULL && strstr(buf, "недоступн") == NULL);
    enc_core_format_status(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "4 з 4 доступні") != NULL && strstr(buf, "недоступні:") == NULL);
    enc_core_set_unavailable(&s, ENC_FR, "x");
    enc_core_set_unavailable(&s, ENC_RR, "y");
    enc_core_format_reset(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "FL, RL") != NULL);
    CHECK(strstr(buf, "недоступні: FR, RR") != NULL);
    enc_core_format_status(&s, buf, sizeof(buf));
    CHECK(strstr(buf, "2 з 4 доступні, недоступні: FR, RR") != NULL);
    CHECK(strstr(buf, "x") == NULL);   // status без причини
}

static void test_format_truncates(void)
{
    enc_state_t s;
    moved(&s, 100);
    char buf[24];
    memset(buf, 'Z', sizeof(buf));
    size_t n = enc_core_format_snapshot(&s, buf, 16);
    CHECK(n < 16 && buf[n] == '\0' && buf[16] == 'Z');
    n = enc_core_format_status(&s, buf, 8);
    CHECK(n < 8 && buf[n] == '\0');
    CHECK(enc_core_format_reset(&s, buf, 0) == 0);
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
    test_speed_catchup_burst();
    test_speed_timestamp_wrap();
    test_reset_keeps_speed();
    test_interval_count_10s();
    test_units();
    test_snapshot_format();
    test_snapshot_negative();
    test_snapshot_before_first_sample();
    test_unavailable_wheel_in_snapshot();
    test_reason_not_copied();
    test_all_unavailable();
    test_reset_and_status_texts();
    test_format_truncates();
    return test_summary();
}
