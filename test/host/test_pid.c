// Хостові тести pid_core (phase5-pid; spec AC-04, AC-07, AC-18; ADR-0002): набір коефіцієнтів, перевірка чисел,
// режим, запис для памʼяті плати з версією і CRC32.
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "pid_core.h"
#include "test_support.h"

static void full_set(pid_set_t *s)
{
    pid_set_init(s);
    CHECK(pid_set_param(s, PID_PARAM_KP, 0.004f) == NULL);
    CHECK(pid_set_param(s, PID_PARAM_KI, 0.02f) == NULL);
    CHECK(pid_set_param(s, PID_PARAM_KD, 0.0f) == NULL);
    CHECK(pid_set_param(s, PID_PARAM_RAMP, 300.0f) == NULL);
}

static void test_init_and_complete(void)
{
    pid_set_t s;
    memset(&s, 0xA5, sizeof s);
    pid_set_init(&s);
    CHECK(s.mode == PID_MODE_UNCONFIGURED);
    CHECK(!s.saved);
    CHECK(!pid_set_complete(&s));
    for (int i = 0; i < PID_PARAM_COUNT; i++) CHECK(!s.set[i]);

    char buf[64];
    pid_set_missing(&s, buf, sizeof buf);
    CHECK(strcmp(buf, "kp, ki, kd, ramp") == 0);

    CHECK(pid_set_param(&s, PID_PARAM_KP, 1.2f) == NULL);
    CHECK(s.set[PID_PARAM_KP] && s.v[PID_PARAM_KP] == 1.2f);
    CHECK(s.mode == PID_MODE_UNCONFIGURED);
    pid_set_missing(&s, buf, sizeof buf);
    CHECK(strcmp(buf, "ki, kd, ramp") == 0);

    full_set(&s);
    CHECK(pid_set_complete(&s));
    CHECK(s.mode == PID_MODE_OFF);   // повний набір уперше → «вимкнений»
    pid_set_missing(&s, buf, sizeof buf);
    CHECK(buf[0] == 0);
}

static void test_names(void)
{
    CHECK(strcmp(pid_param_name(PID_PARAM_KP), "kp") == 0);
    CHECK(strcmp(pid_param_name(PID_PARAM_KI), "ki") == 0);
    CHECK(strcmp(pid_param_name(PID_PARAM_KD), "kd") == 0);
    CHECK(strcmp(pid_param_name(PID_PARAM_RAMP), "ramp") == 0);
    pid_param_t p = PID_PARAM_COUNT;
    CHECK(pid_param_parse("ramp", 4, &p) && p == PID_PARAM_RAMP);
    CHECK(pid_param_parse("ki", 2, &p) && p == PID_PARAM_KI);
    CHECK(!pid_param_parse("kpx", 3, &p));
    CHECK(!pid_param_parse("limit", 5, &p));
    CHECK(!pid_param_parse("k", 1, &p));
    CHECK(!pid_param_parse("KP", 2, &p));
}

static void test_value_check(void)
{
    // AC-04: коефіцієнти скінченні й невідʼємні, темп розгону — додатний.
    CHECK(pid_value_check(PID_PARAM_KP, 0.0f) == NULL);
    CHECK(pid_value_check(PID_PARAM_KD, 5.0f) == NULL);
    CHECK(pid_value_check(PID_PARAM_KP, -1.0f) != NULL);
    CHECK(pid_value_check(PID_PARAM_KI, NAN) != NULL);
    CHECK(pid_value_check(PID_PARAM_KD, INFINITY) != NULL);
    CHECK(pid_value_check(PID_PARAM_RAMP, 0.0f) != NULL);
    CHECK(pid_value_check(PID_PARAM_RAMP, -10.0f) != NULL);
    CHECK(pid_value_check(PID_PARAM_RAMP, 0.5f) == NULL);

    // Відмова не змінює набір.
    pid_set_t s;
    full_set(&s);
    pid_set_t before = s;
    CHECK(pid_set_param(&s, PID_PARAM_KP, -1.0f) != NULL);
    CHECK(pid_set_param(&s, PID_PARAM_RAMP, 0.0f) != NULL);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
}

static void test_mode(void)
{
    pid_set_t s;
    pid_set_init(&s);
    pid_set_param(&s, PID_PARAM_KP, 1.0f);
    CHECK(pid_set_mode(&s, true) != NULL);           // неповний набір — відмова
    CHECK(s.mode == PID_MODE_UNCONFIGURED);
    full_set(&s);
    s.saved = true;
    CHECK(pid_set_mode(&s, true) == NULL);
    CHECK(s.mode == PID_MODE_ON);
    CHECK(!s.saved);                                  // зміна режиму знімає «збережено»
    CHECK(pid_set_mode(&s, false) == NULL);
    CHECK(s.mode == PID_MODE_OFF);
    s.saved = true;
    CHECK(pid_set_param(&s, PID_PARAM_KI, 0.5f) == NULL);
    CHECK(!s.saved);                                  // зміна числа знімає «збережено»
    CHECK(s.mode == PID_MODE_OFF);                    // і не змінює режим
}

static void test_crc32(void)
{
    // Відоме значення CRC-32/ISO-HDLC для «123456789».
    CHECK(pid_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
    CHECK(pid_crc32((const uint8_t *)"", 0) == 0x00000000u);
}

static void test_record_roundtrip(void)
{
    pid_set_t s;
    full_set(&s);
    pid_set_mode(&s, true);
    uint8_t rec[PID_RECORD_SIZE];
    CHECK(pid_record_encode(&s, rec) == PID_RECORD_SIZE);
    CHECK(rec[0] == 0x31 && rec[1] == 0x44 && rec[2] == 0x49 && rec[3] == 0x50);   // little-endian «PID1»
    CHECK(rec[4] == PID_RECORD_VERSION);

    pid_set_t out;
    pid_set_init(&out);
    CHECK(pid_record_decode(rec, sizeof rec, &out) == PID_REC_OK);
    CHECK(out.mode == PID_MODE_ON);
    CHECK(out.saved);
    CHECK(pid_set_complete(&out));
    for (int i = 0; i < PID_PARAM_COUNT; i++) CHECK(out.v[i] == s.v[i]);

    // Режим «вимкнений» зберігається теж (spec AC-06).
    pid_set_mode(&s, false);
    pid_record_encode(&s, rec);
    pid_set_init(&out);
    CHECK(pid_record_decode(rec, sizeof rec, &out) == PID_REC_OK && out.mode == PID_MODE_OFF);

    // Неповний набір не кодується (spec AC-06: збереження неповного набору відхиляється).
    pid_set_t part;
    pid_set_init(&part);
    pid_set_param(&part, PID_PARAM_KP, 1.0f);
    CHECK(pid_record_encode(&part, rec) == 0);
}

static void test_record_rejects(void)
{
    pid_set_t s;
    full_set(&s);
    pid_set_mode(&s, true);
    uint8_t good[PID_RECORD_SIZE];
    pid_record_encode(&s, good);

    pid_set_t out, untouched;
    pid_set_init(&out);
    pid_set_param(&out, PID_PARAM_KP, 7.0f);
    untouched = out;

    CHECK(pid_record_decode(NULL, 0, &out) == PID_REC_ABSENT);
    CHECK(pid_record_decode(good, 0, &out) == PID_REC_ABSENT);
    CHECK(pid_record_decode(good, PID_RECORD_SIZE - 1, &out) == PID_REC_SIZE);    // неповний запис

    uint8_t bad[PID_RECORD_SIZE];
    for (unsigned i = 0; i < PID_RECORD_SIZE; i++) {   // будь-який змінений байт не проходить
        memcpy(bad, good, sizeof bad);
        bad[i] ^= 0x01;
        CHECK(pid_record_decode(bad, sizeof bad, &out) != PID_REC_OK);
    }
    memcpy(bad, good, sizeof bad);
    bad[0] ^= 0xFF;
    CHECK(pid_record_decode(bad, sizeof bad, &out) == PID_REC_MAGIC);
    memcpy(bad, good, sizeof bad);
    bad[10] ^= 0x10;                                                             // число всередині
    CHECK(pid_record_decode(bad, sizeof bad, &out) == PID_REC_CRC);

    // Інша версія з правильним CRC — відкинуто як інша версія.
    memcpy(bad, good, sizeof bad);
    bad[4] = 2;
    uint32_t c = pid_crc32(bad, PID_RECORD_SIZE - 4);
    bad[24] = (uint8_t)c; bad[25] = (uint8_t)(c >> 8); bad[26] = (uint8_t)(c >> 16); bad[27] = (uint8_t)(c >> 24);
    CHECK(pid_record_decode(bad, sizeof bad, &out) == PID_REC_VERSION);

    // Відʼємне число з правильним CRC — відкинуто як недопустиме значення.
    memcpy(bad, good, sizeof bad);
    float neg = -1.0f;
    memcpy(&bad[8], &neg, 4);
    c = pid_crc32(bad, PID_RECORD_SIZE - 4);
    bad[24] = (uint8_t)c; bad[25] = (uint8_t)(c >> 8); bad[26] = (uint8_t)(c >> 16); bad[27] = (uint8_t)(c >> 24);
    CHECK(pid_record_decode(bad, sizeof bad, &out) == PID_REC_VALUE);

    // Невідомий режим з правильним CRC — відкинуто.
    memcpy(bad, good, sizeof bad);
    bad[5] = 9;
    c = pid_crc32(bad, PID_RECORD_SIZE - 4);
    bad[24] = (uint8_t)c; bad[25] = (uint8_t)(c >> 8); bad[26] = (uint8_t)(c >> 16); bad[27] = (uint8_t)(c >> 24);
    CHECK(pid_record_decode(bad, sizeof bad, &out) == PID_REC_VALUE);

    // Жоден відкинутий запис не змінив набір (spec AC-07: жодне число з нього не завантажується).
    CHECK(memcmp(&out, &untouched, sizeof out) == 0);

    CHECK(strlen(pid_record_result_text(PID_REC_ABSENT)) > 0);
    CHECK(strlen(pid_record_result_text(PID_REC_CRC)) > 0);
    CHECK(strcmp(pid_record_result_text(PID_REC_CRC), pid_record_result_text(PID_REC_SIZE)) != 0);
}

// ---------- T2: крок регулятора ----------

// Модель борту першого порядку: усталена швидкість = 210 об/хв на одиницю потужності мінус навантаження, τ = 80 мс.
typedef struct { float speed; } plant_t;
static void plant_step(plant_t *p, float u, float load, float dt)
{
    float sign = u >= 0 ? 1.0f : -1.0f;
    float drive = 210.0f * u - sign * load;
    p->speed += (drive - p->speed) * dt / 0.08f;
}

static void gains(pid_set_t *s, float kp, float ki, float kd, float ramp)
{
    pid_set_init(s);
    pid_set_param(s, PID_PARAM_KP, kp);
    pid_set_param(s, PID_PARAM_KI, ki);
    pid_set_param(s, PID_PARAM_KD, kd);
    pid_set_param(s, PID_PARAM_RAMP, ramp);
}

static float fmax_(float a, float b) { return a > b ? a : b; }   // без libm: раннер не лінкує -lm

static int within(float v, float target, float frac) { return fabsf(v - target) <= frac * fabsf(target); }

static void test_target_and_ramp(void)
{
    CHECK(pid_target_from_mix(1.0f) == PID_TARGET_MAX_RPM);
    CHECK(pid_target_from_mix(0.5f) == 75.0f);
    CHECK(pid_target_from_mix(-1.0f) == -PID_TARGET_MAX_RPM);
    CHECK(pid_target_from_mix(1.7f) == PID_TARGET_MAX_RPM);
    CHECK(pid_target_from_mix(-3.0f) == -PID_TARGET_MAX_RPM);
    CHECK(pid_target_from_mix(0.0f) == 0.0f);

    // AC-13: зростання не швидше за темп, зменшення одразу, зміна знака через нуль.
    CHECK(fabsf(pid_ramp(0.0f, 150.0f, 300.0f, 0.02f) - 6.0f) < 1e-4f);
    CHECK(fabsf(pid_ramp(6.0f, 150.0f, 300.0f, 0.02f) - 12.0f) < 1e-4f);
    CHECK(pid_ramp(149.0f, 150.0f, 300.0f, 0.02f) == 150.0f);
    CHECK(pid_ramp(150.0f, 60.0f, 300.0f, 0.02f) == 60.0f);
    CHECK(pid_ramp(150.0f, 0.0f, 300.0f, 0.02f) == 0.0f);
    CHECK(fabsf(pid_ramp(150.0f, -150.0f, 300.0f, 0.02f) - (-6.0f)) < 1e-4f);
    CHECK(fabsf(pid_ramp(-6.0f, -150.0f, 300.0f, 0.02f) - (-12.0f)) < 1e-4f);
    float t = 0.0f;
    int ticks = 0;
    while (t < 150.0f && ticks < 1000) { t = pid_ramp(t, 150.0f, 300.0f, 0.02f); ticks++; }
    CHECK(ticks == 25);   // 150 об/хв при 300 об/хв/с — 0.5 с = 25 тактів
}

static void test_side_speed(void)
{
    float rpm = -1.0f;
    CHECK(pid_side_speed(true, 100.0f, true, 80.0f, &rpm) == PID_SRC_TWO && rpm == 90.0f);
    CHECK(pid_side_speed(false, 100.0f, true, 80.0f, &rpm) == PID_SRC_ONE && rpm == 80.0f);
    CHECK(pid_side_speed(true, 100.0f, false, 80.0f, &rpm) == PID_SRC_ONE && rpm == 100.0f);
    CHECK(pid_side_speed(false, 100.0f, false, 80.0f, &rpm) == PID_SRC_NONE && rpm == 0.0f);
}

// AC-01: утримання ±5 % під навантаженням, повернення за ≤ 0.5 с, перевищення ≤ 20 %.
static void test_hold_under_load(void)
{
    pid_set_t g;
    gains(&g, 0.003f, 0.05f, 0.0f, 300.0f);
    pid_side_t st;
    pid_side_reset(&st);
    plant_t p = {0};
    const float dt = 0.02f, target = 90.0f, lim = 0.75f;
    float u = 0.0f, worst_ss = 0.0f, worst_over = 0.0f;
    int last_out_of_band_after_load = -1, last_out_of_band_after_release = -1;
    for (int k = 0; k < 500; k++) {   // 10 с
        float load = (k >= 200 && k < 350) ? 30.0f : 0.0f;   // палець 4…7 с
        u = pid_side_step(&st, &g, target, p.speed, dt, lim);
        CHECK(u >= 0.0f && u <= lim);
        plant_step(&p, u, load, dt);
        if (k >= 150 && k < 200) worst_ss = fmax_(worst_ss, fabsf(p.speed - target));
        if (k >= 200 && k < 350 && !within(p.speed, target, 0.05f)) last_out_of_band_after_load = k;
        if (k >= 350 && !within(p.speed, target, 0.05f)) last_out_of_band_after_release = k;
        if (k >= 350) worst_over = fmax_(worst_over, p.speed - target);
    }
    CHECK(worst_ss <= 0.05f * target);
    CHECK(last_out_of_band_after_load >= 200 && (last_out_of_band_after_load - 200) * dt <= 0.5f);
    CHECK(last_out_of_band_after_release < 0 || (last_out_of_band_after_release - 350) * dt <= 0.5f);
    CHECK(worst_over <= 0.20f * target);
}

// AC-09, AC-10: колесо утримується до зупинки — вихід на межі, накопичена складова не росте; після відпускання
// перевищення ≤ 20 % і повернення в ±5 % за ≤ 0.5 с.
static void test_saturation_and_release(void)
{
    pid_set_t g;
    gains(&g, 0.003f, 0.05f, 0.0f, 300.0f);
    pid_side_t st;
    pid_side_reset(&st);
    plant_t p = {0};
    const float dt = 0.02f, target = 90.0f, lim = 0.75f;
    for (int k = 0; k < 150; k++) plant_step(&p, pid_side_step(&st, &g, target, p.speed, dt, lim), 0.0f, dt);
    float integ_at_stop = -1.0f;
    for (int k = 0; k < 100; k++) {   // 2 с утримання: швидкість 0
        float u = pid_side_step(&st, &g, target, 0.0f, dt, lim);
        CHECK(u <= lim);
        if (k == 50) integ_at_stop = st.integ;
        if (k == 99) {
            CHECK(u == lim);
            CHECK(st.integ <= lim);
            CHECK(st.integ == integ_at_stop);   // на межі не росте
        }
    }
    p.speed = 0.0f;
    float worst_over = 0.0f;
    int last_out = -1;
    for (int k = 0; k < 100; k++) {
        plant_step(&p, pid_side_step(&st, &g, target, p.speed, dt, lim), 0.0f, dt);
        worst_over = fmax_(worst_over, p.speed - target);
        if (!within(p.speed, target, 0.05f)) last_out = k;
    }
    CHECK(worst_over <= 0.20f * target);
    CHECK((last_out + 1) * dt <= 0.5f);
}

// AC-09: межа не перевищується за будь-якого набору; напрямок виходу — лише напрямок цілі.
static void test_limit_and_direction(void)
{
    pid_set_t g;
    gains(&g, 50.0f, 50.0f, 50.0f, 1000.0f);
    pid_side_t st;
    pid_side_reset(&st);
    const float lim = 0.75f;
    float speeds[] = {0, 10, 150, -150, 300, -40, 90};
    for (unsigned i = 0; i < sizeof speeds / sizeof speeds[0]; i++) {
        float u = pid_side_step(&st, &g, 120.0f, speeds[i], 0.02f, lim);
        CHECK(u >= 0.0f && u <= lim);
        float v = pid_side_step(&st, &g, -120.0f, -speeds[i], 0.02f, lim);
        CHECK(v <= 0.0f && v >= -lim);
    }
    // Борт швидший за задану, накопиченого немає → вихід 0 (вибіг, без зворотної потужності).
    gains(&g, 0.003f, 0.05f, 0.0f, 300.0f);
    pid_side_reset(&st);
    CHECK(pid_side_step(&st, &g, 90.0f, 150.0f, 0.02f, lim) == 0.0f);
}

static void test_zero_target_and_reversal(void)
{
    pid_set_t g;
    gains(&g, 0.003f, 0.05f, 0.0f, 300.0f);
    pid_side_t st;
    pid_side_reset(&st);
    for (int k = 0; k < 50; k++) pid_side_step(&st, &g, 90.0f, 40.0f, 0.02f, 0.75f);
    CHECK(st.integ > 0.0f);
    CHECK(pid_side_step(&st, &g, 0.0f, 40.0f, 0.02f, 0.75f) == 0.0f);   // AC-13: нуль одразу
    CHECK(st.integ == 0.0f && !st.has_prev && st.dir == 0);
    for (int k = 0; k < 50; k++) pid_side_step(&st, &g, 90.0f, 40.0f, 0.02f, 0.75f);
    float u = pid_side_step(&st, &g, -90.0f, 40.0f, 0.02f, 0.75f);       // зміна знака скидає накопичене
    CHECK(u <= 0.0f);
    CHECK(st.integ <= 0.05f * 130.0f * 0.02f + 1e-6f);
}

// Зміна Ki на ходу без стрибка; D від виміру без удару від стрибка цілі; перший крок після скидання без D.
static void test_bumpless(void)
{
    pid_set_t g;
    gains(&g, 0.0f, 0.05f, 0.0f, 300.0f);
    pid_side_t st;
    pid_side_reset(&st);
    float u1 = 0.0f;
    for (int k = 0; k < 50; k++) u1 = pid_side_step(&st, &g, 90.0f, 60.0f, 0.02f, 0.75f);
    pid_set_param(&g, PID_PARAM_KI, 0.2f);
    float u2 = pid_side_step(&st, &g, 90.0f, 60.0f, 0.02f, 0.75f);
    CHECK(fabsf(u2 - u1) <= 0.2f * 30.0f * 0.02f + 1e-5f);   // лише новий приріст, без перерахунку накопиченого

    gains(&g, 0.0f, 0.0f, 0.001f, 300.0f);
    pid_side_reset(&st);
    float a = pid_side_step(&st, &g, 50.0f, 50.0f, 0.02f, 0.75f);
    CHECK(a == 0.0f);                                          // перший крок: D = 0 навіть при ненульовій швидкості
    float b = pid_side_step(&st, &g, 50.0f, 50.0f, 0.02f, 0.75f);
    float c = pid_side_step(&st, &g, 100.0f, 50.0f, 0.02f, 0.75f);
    CHECK(b == c);                                             // стрибок цілі не дає удару D

    // dt обмежується: dt = 0 і dt = 1 с дають приріст накопиченого як за 10 мс і 40 мс.
    gains(&g, 0.0f, 1.0f, 0.0f, 300.0f);
    pid_side_reset(&st);
    pid_side_step(&st, &g, 50.0f, 40.0f, 0.0f, 0.75f);
    CHECK(fabsf(st.integ - 10.0f * 0.010f) < 1e-5f);
    pid_side_reset(&st);
    pid_side_step(&st, &g, 50.0f, 49.0f, 1.0f, 0.75f);
    CHECK(fabsf(st.integ - 1.0f * 0.040f) < 1e-5f);
}

// ---------- T3: кадр трасування і показ стану ----------

static void view(pid_side_view_t *v, pid_side_mode_t m, const char *missing, float tgt, float spd, float out, float mix)
{
    v->mode = m; v->missing = missing; v->target_rpm = tgt; v->speed_rpm = spd; v->out = out; v->mix = mix;
}

static void test_trace_acc(void)
{
    pid_trace_acc_t a;
    pid_trace_acc_reset(&a);
    pid_side_view_t side[2];
    pid_trace_frame_t f;
    memset(&f, 0, sizeof f);
    for (int k = 0; k < PID_TRACE_TICKS - 1; k++) {
        view(&side[0], PID_SIDE_TWO, NULL, 90.0f, 80.0f + (float)k, 0.40f, 0.6f);
        view(&side[1], PID_SIDE_ONE, "RR", 90.0f, 90.0f, 0.50f, 0.6f);
        CHECK(!pid_trace_acc_add(&a, side, 1000u + 20u * (unsigned)k, true, &f));
    }
    view(&side[0], PID_SIDE_TWO, NULL, 90.0f, 84.0f, 0.40f, 0.6f);
    view(&side[1], PID_SIDE_ONE, "RR", 90.0f, 90.0f, 0.50f, 0.6f);
    CHECK(pid_trace_acc_add(&a, side, 1080u, true, &f));
    CHECK(f.t_ms == 1080u && f.armed && f.seq == 1u);
    CHECK(fabsf(f.speed_rpm[PID_LEFT] - 82.0f) < 1e-4f);   // середнє 80…84
    CHECK(fabsf(f.out[PID_RIGHT] - 0.50f) < 1e-5f);
    CHECK(f.mode[PID_RIGHT] == PID_SIDE_ONE && strcmp(f.missing[PID_RIGHT], "RR") == 0);
    CHECK(!pid_trace_acc_add(&a, side, 1100u, true, &f));   // суми скинуто, новий кадр — через 5 тактів
    for (int k = 0; k < PID_TRACE_TICKS - 1; k++) pid_trace_acc_add(&a, side, 1120u, true, &f);
    CHECK(f.seq == 2u);
}

static void test_format_trace(void)
{
    pid_trace_frame_t f;
    memset(&f, 0, sizeof f);
    f.t_ms = 12345u; f.armed = true;
    f.mode[0] = PID_SIDE_TWO; f.target_rpm[0] = 90.0f; f.speed_rpm[0] = 88.74f; f.out[0] = 0.4234f;
    f.mode[1] = PID_SIDE_OPEN; f.speed_rpm[1] = 70.0f; f.mix[1] = 0.6f;
    char buf[200];
    size_t len = pid_format_trace(&f, buf, sizeof buf);
    CHECK(len == strlen(buf) && len > 0);
    CHECK(strstr(buf, "12345") != NULL);
    CHECK(strstr(buf, "90.0") != NULL && strstr(buf, "88.7") != NULL && strstr(buf, "+0.42") != NULL);
    CHECK(strstr(buf, "без регулятора") != NULL && strstr(buf, "70.0") != NULL && strstr(buf, "+0.60") != NULL);
    f.mode[1] = PID_SIDE_NO_ENC;
    pid_format_trace(&f, buf, sizeof buf);
    CHECK(strstr(buf, "без енкодерів") != NULL && strstr(buf, "70.0") == NULL);
    char small[8];
    CHECK(pid_format_trace(&f, small, sizeof small) == strlen(small));   // обрізання з нулем у кінці
}

static void test_format_show(void)
{
    char buf[512];
    pid_set_t s;
    pid_set_init(&s);
    pid_set_param(&s, PID_PARAM_KP, 0.003f);
    pid_format_show(&s, NULL, PID_REC_OK, buf, sizeof buf);
    CHECK(strstr(buf, "не налаштований") != NULL);
    CHECK(strstr(buf, "ki —") != NULL);                       // незадане число позначено
    CHECK(strstr(buf, "ki, kd, ramp") != NULL);               // і перелічене
    full_set(&s);
    pid_set_mode(&s, true);
    pid_side_view_t side[2];
    view(&side[0], PID_SIDE_TWO, NULL, 90.0f, 88.7f, 0.42f, 0.6f);
    view(&side[1], PID_SIDE_ONE, "FR", 90.0f, 91.2f, 0.45f, 0.6f);
    pid_format_show(&s, side, PID_REC_OK, buf, sizeof buf);
    CHECK(strstr(buf, "увімкнений") != NULL && strstr(buf, "не збережено") != NULL);
    CHECK(strstr(buf, "за двома колесами") != NULL);
    CHECK(strstr(buf, "за одним колесом") != NULL && strstr(buf, "FR") != NULL);
    CHECK(strstr(buf, "88.7") != NULL && strstr(buf, "+0.45") != NULL);
    s.saved = true;
    pid_set_mode(&s, false);
    s.saved = true;
    view(&side[0], PID_SIDE_NO_ENC, NULL, 0, 0, 0, 0);
    view(&side[1], PID_SIDE_OPEN, NULL, 0, 10.0f, 0, 0.1f);
    pid_format_show(&s, side, PID_REC_OK, buf, sizeof buf);
    CHECK(strstr(buf, "вимкнений") != NULL && strstr(buf, "(збережено)") != NULL);
    CHECK(strstr(buf, "без енкодерів") != NULL && strstr(buf, "без регулятора") != NULL);
}

static void test_save_finish(void)
{
    // Рев'ю F3 (AC-08): збій сховища — набір і режим діють далі, текст називає помилку.
    // Рев'ю N2: збій знімає «збережено», навіть якщо набір був збережений до спроби.
    pid_set_t s, snap;
    char buf[200];
    full_set(&s);
    CHECK(pid_set_mode(&s, true) == NULL);
    s.saved = true;   // старт із правильним записом
    snap = s;
    pid_save_mark(&s, &snap, false);
    size_t len = pid_format_save(&snap, "ESP_ERR_NVS_NOT_ENOUGH_SPACE", buf, sizeof buf);
    CHECK(len == strlen(buf) && len > 0);
    CHECK(strstr(buf, "не збережено") != NULL && strstr(buf, "ESP_ERR_NVS_NOT_ENOUGH_SPACE") != NULL);
    CHECK(strstr(buf, "діє далі") != NULL);
    CHECK(!s.saved && s.mode == PID_MODE_ON && s.v[PID_PARAM_KP] == 0.004f && pid_set_complete(&s));
    // Те саме зі стартовим saved=false.
    s.saved = false;
    pid_save_mark(&s, &snap, false);
    CHECK(!s.saved);
    // Успіх: «збережено» і перелік чисел.
    pid_save_mark(&s, &snap, true);
    len = pid_format_save(&snap, NULL, buf, sizeof buf);
    CHECK(len == strlen(buf));
    CHECK(s.saved && strstr(buf, "Набір збережено") != NULL && strstr(buf, "увімкнений") != NULL);
    // Набір змінився, поки йшов запис: успіх запису, але «збережено» не ставиться.
    s.saved = false;
    CHECK(pid_set_param(&s, PID_PARAM_KP, 0.005f) == NULL);
    pid_save_mark(&s, &snap, true);
    CHECK(!s.saved);
}

static void test_show_load_result(void)
{
    // Рев'ю F4 (AC-07): показ стану називає відкинутий запис; F9: збій читання — окремий текст.
    pid_set_t s;
    char buf[400];
    pid_set_init(&s);
    pid_format_show(&s, NULL, PID_REC_CRC, buf, sizeof buf);
    CHECK(strstr(buf, "не налаштований") != NULL && strstr(buf, "запис відкинуто") != NULL);
    CHECK(strstr(buf, "контрольна сума") != NULL);
    pid_format_show(&s, NULL, PID_REC_SIZE, buf, sizeof buf);
    CHECK(strstr(buf, "запис відкинуто") != NULL);
    pid_format_show(&s, NULL, PID_REC_ABSENT, buf, sizeof buf);
    CHECK(strstr(buf, "відкинуто") == NULL && strstr(buf, "не налаштований") != NULL);
    pid_format_show(&s, NULL, PID_REC_READ, buf, sizeof buf);
    CHECK(strstr(buf, "не прочитано") != NULL);
    CHECK(strstr(pid_record_result_text(PID_REC_READ), "не прочитано") != NULL);
    pid_format_show(&s, NULL, PID_REC_OK, buf, sizeof buf);
    CHECK(strstr(buf, "відкинуто") == NULL && strstr(buf, "не прочитано") == NULL);
}

static void test_arm_save_guard(void)
{
    // Рев'ю F1 (AC-15): вхід в ARMED і запис набору взаємно виключні; перевірка й позначка — одна атомарна дія.
    // Рев'ю R1: зміна режиму — окрема ознака; озброєння дізнається, що саме йому заважає.
    pid_guard_t g;
    pid_busy_t why;
    pid_guard_init(&g);
    CHECK(!g.saving && !g.arming && !g.changing);
    CHECK(pid_guard_try_save(&g) && g.saving);
    why = PID_BUSY_NONE;
    CHECK(!pid_guard_try_arm(&g, &why) && !g.arming && why == PID_BUSY_SAVE);   // збереження триває
    CHECK(!pid_guard_try_mode(&g) && !g.changing);
    pid_guard_end_save(&g);
    CHECK(!g.saving);
    CHECK(pid_guard_try_arm(&g, &why) && g.arming && why == PID_BUSY_NONE);
    CHECK(!pid_guard_try_save(&g) && !g.saving);     // озброєння триває — збереження відхиляється
    CHECK(!pid_guard_try_mode(&g) && !g.changing);   // і зміна режиму теж
    pid_guard_end_arm(&g);
    CHECK(!g.arming && pid_guard_try_save(&g));
    CHECK(!pid_guard_try_save(&g));                  // друге збереження поверх першого — теж ні
    pid_guard_end_save(&g);
    // Зміна режиму триває: озброєння — з причиною «зміна режиму», збереження й друга зміна — ні.
    CHECK(pid_guard_try_mode(&g) && g.changing && !g.saving);
    CHECK(!pid_guard_try_arm(&g, &why) && !g.arming && why == PID_BUSY_MODE);
    CHECK(!pid_guard_try_save(&g) && !pid_guard_try_mode(&g));
    pid_guard_end_mode(&g);
    CHECK(!g.changing && pid_guard_try_arm(&g, &why) && why == PID_BUSY_NONE);
}

static void test_mode_change_disarmed_only(void)
{
    // Рев'ю N4 (AC-15): стан перевіряється атомарно із записом режиму; поза DISARMED режим і позначка не змінюються.
    pid_set_t s;
    full_set(&s);
    CHECK(pid_set_mode(&s, true) == NULL);
    s.saved = true;
    CHECK(pid_mode_change(&s, false, false) != NULL);
    CHECK(s.mode == PID_MODE_ON && s.saved);
    CHECK(pid_mode_change(&s, false, true) == NULL);
    CHECK(s.mode == PID_MODE_OFF && !s.saved);
    // Неповний набір у DISARMED — та сама відмова, що й у pid_set_mode.
    pid_set_init(&s);
    CHECK(pid_mode_change(&s, true, true) != NULL && s.mode == PID_MODE_UNCONFIGURED);
    // Поза DISARMED причина інша, ніж «набір неповний».
    full_set(&s);
    const char *why = pid_mode_change(&s, true, false);
    CHECK(why != NULL && strstr(why, "неповний") == NULL && strstr(why, "DISARMED") != NULL);
}

int main(void)
{
    test_mode_change_disarmed_only();
    test_arm_save_guard();
    test_save_finish();
    test_show_load_result();
    test_init_and_complete();
    test_names();
    test_value_check();
    test_mode();
    test_crc32();
    test_record_roundtrip();
    test_record_rejects();
    test_target_and_ramp();
    test_side_speed();
    test_hold_under_load();
    test_saturation_and_release();
    test_limit_and_direction();
    test_zero_target_and_reversal();
    test_bumpless();
    test_trace_acc();
    test_format_trace();
    test_format_show();
    return test_summary();
}
