// Хостові тести crsf_core (spec AC-06, AC-10, AC-11; sad.md §4): збирач кадрів, CRC8, розпакування, лічильники.
#include <stdint.h>
#include <string.h>

#include <math.h>

#include "crsf_calib.h"
#include "crsf_core.h"
#include "test_support.h"

// Незалежний пакувальник для тестів: 16 каналів по 11 біт, молодший біт першим.
static void pack_channels(const uint16_t ch[CRSF_CHANNELS], uint8_t out[22])
{
    memset(out, 0, 22);
    unsigned bit = 0;
    for (int i = 0; i < CRSF_CHANNELS; i++) {
        for (int b = 0; b < 11; b++, bit++) {
            if (ch[i] & (1u << b)) out[bit / 8] |= (uint8_t)(1u << (bit % 8));
        }
    }
}

// Збирає кадр [C8][len][type][payload][crc]; повертає загальну довжину.
static size_t make_frame(uint8_t *f, uint8_t type, const uint8_t *payload, size_t plen)
{
    f[0] = CRSF_SYNC_BYTE;
    f[1] = (uint8_t)(plen + 2);
    f[2] = type;
    memcpy(f + 3, payload, plen);
    f[3 + plen] = crsf_crc8(f + 2, plen + 1);
    return plen + 4;
}

static size_t rc_frame(uint8_t *f, const uint16_t ch[CRSF_CHANNELS])
{
    uint8_t pl[22];
    pack_channels(ch, pl);
    return make_frame(f, CRSF_TYPE_RC_CHANNELS, pl, 22);
}

static void all_centered(uint16_t ch[CRSF_CHANNELS])
{
    for (int i = 0; i < CRSF_CHANNELS; i++) ch[i] = CRSF_RAW_CENTER;
}

static void test_crc8(void)
{
    // Відомий вектор CRC-8/DVB-S2 (поліном 0xD5) для "123456789".
    const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crsf_crc8(v, sizeof(v)) == 0xBC);
    CHECK(crsf_crc8(v, 0) == 0);
}

static void test_reference_frame(void)
{
    // Центр усіх каналів — відомий шаблон протоколу: E0 03 1F F8 C0 07 3E F0 81 0F 7C (двічі).
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    uint8_t pl[22];
    pack_channels(ch, pl);
    const uint8_t pat[11] = {0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C};
    CHECK(memcmp(pl, pat, 11) == 0);
    CHECK(memcmp(pl + 11, pat, 11) == 0);

    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    CHECK(n == 26);
    crsf_parser_t p;
    crsf_parser_init(&p);
    CHECK(crsf_feed(&p, f, n, 1234) == 1);
    CHECK(p.snap.have_frame);
    CHECK(p.snap.seq == 1);
    CHECK(p.snap.t_ms == 1234);
    CHECK(p.snap.cnt.rc_ok == 1);
    for (int i = 0; i < CRSF_CHANNELS; i++) CHECK(p.snap.ch[i] == CRSF_RAW_CENTER);
}

static void test_unpack_values(void)
{
    uint16_t ch[CRSF_CHANNELS];
    for (int i = 0; i < CRSF_CHANNELS; i++) ch[i] = (uint16_t)(172 + i * 100);
    ch[4] = 1792;
    ch[0] = 1006;
    ch[1] = 1808;
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    crsf_parser_t p;
    crsf_parser_init(&p);
    CHECK(crsf_feed(&p, f, n, 5) == 1);
    for (int i = 0; i < CRSF_CHANNELS; i++) CHECK(p.snap.ch[i] == ch[i]);
}

static void test_split_chunks(void)
{
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    ch[1] = 1500;
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    crsf_parser_t p;
    crsf_parser_init(&p);
    size_t got = 0;
    // Кадр розрізаний між двома порціями читання.
    got += crsf_feed(&p, f, 10, 100);
    CHECK(got == 0);
    CHECK(p.snap.cnt.rc_ok == 0);
    got += crsf_feed(&p, f + 10, n - 10, 110);
    CHECK(got == 1);
    CHECK(p.snap.cnt.rc_ok == 1);
    CHECK(p.snap.t_ms == 110);
    CHECK(p.snap.ch[1] == 1500);
    // По одному байту.
    got = 0;
    for (size_t i = 0; i < n; i++) got += crsf_feed(&p, f + i, 1, 200);
    CHECK(got == 1);
    CHECK(p.snap.cnt.rc_ok == 2);
}

static void test_bad_crc(void)
{
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    f[n - 1] ^= 0x01;
    crsf_parser_t p;
    crsf_parser_init(&p);
    CHECK(crsf_feed(&p, f, n, 10) == 0);
    CHECK(p.snap.cnt.crc_fail == 1);
    CHECK(p.snap.cnt.rc_ok == 0);
    CHECK(!p.snap.have_frame);
    CHECK(p.snap.t_ms == 0);
    // Наступний валідний кадр приймається як звичайний.
    n = rc_frame(f, ch);
    CHECK(crsf_feed(&p, f, n, 20) == 1);
    CHECK(p.snap.cnt.rc_ok == 1);
    CHECK(p.snap.t_ms == 20);
}

static void test_resync_inside_bad_frame(void)
{
    // Битий кадр, у корисному навантаженні якого лежить початок валідного кадру (0xC8 усередині).
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    ch[0] = 1234;
    uint8_t good[CRSF_FRAME_MAX];
    size_t gn = rc_frame(good, ch);
    uint8_t stream[CRSF_FRAME_MAX * 2];
    // Фальшивий початок «C8 18 16», далі справжній кадр цілком: фальшивий кадр має битий CRC.
    stream[0] = CRSF_SYNC_BYTE;
    stream[1] = CRSF_LEN_RC;
    stream[2] = CRSF_TYPE_RC_CHANNELS;
    memcpy(stream + 3, good, gn);
    crsf_parser_t p;
    crsf_parser_init(&p);
    size_t got = crsf_feed(&p, stream, 3 + gn, 50);
    CHECK(got == 1);
    CHECK(p.snap.ch[0] == 1234);
    CHECK(p.snap.cnt.crc_fail >= 1);
}

static void test_wrong_length(void)
{
    crsf_parser_t p;
    crsf_parser_init(&p);
    uint8_t f[CRSF_FRAME_MAX];
    // len поза 2…62.
    const uint8_t bad1[] = {CRSF_SYNC_BYTE, 1, 0x16, 0x00};
    CHECK(crsf_feed(&p, bad1, sizeof(bad1), 1) == 0);
    CHECK(p.snap.cnt.len_fail >= 1);
    uint32_t lf = p.snap.cnt.len_fail;
    const uint8_t bad2[] = {CRSF_SYNC_BYTE, 63, 0x16, 0x00};
    CHECK(crsf_feed(&p, bad2, sizeof(bad2), 2) == 0);
    CHECK(p.snap.cnt.len_fail > lf);
    // Тип 0x16 із коректним CRC, але довжина не 24.
    uint8_t pl[10] = {0};
    size_t n = make_frame(f, CRSF_TYPE_RC_CHANNELS, pl, sizeof(pl));
    crsf_parser_init(&p);
    CHECK(crsf_feed(&p, f, n, 3) == 0);
    CHECK(p.snap.cnt.len_fail == 1);
    CHECK(p.snap.cnt.rc_ok == 0);
    CHECK(!p.snap.have_frame);
}

static void test_other_type_does_not_touch_rc(void)
{
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    crsf_parser_t p;
    crsf_parser_init(&p);
    CHECK(crsf_feed(&p, f, n, 100) == 1);
    // Кадр статистики (тип 0x14, 10 байтів) із правильним CRC.
    uint8_t st[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    n = make_frame(f, 0x14, st, sizeof(st));
    CHECK(crsf_feed(&p, f, n, 900) == 0);
    CHECK(p.snap.cnt.other == 1);
    CHECK(p.snap.t_ms == 100);   // мітка не оновлена
    CHECK(p.snap.seq == 1);
    CHECK(p.snap.cnt.rc_ok == 1);
}

static void test_clamp(void)
{
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    ch[0] = 0;        // нижче робочого діапазону
    ch[1] = 2047;     // вище
    ch[2] = 171;
    ch[3] = 1812;
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    crsf_parser_t p;
    crsf_parser_init(&p);
    CHECK(crsf_feed(&p, f, n, 1) == 1);
    CHECK(p.snap.ch[0] == CRSF_RAW_MIN);
    CHECK(p.snap.ch[1] == CRSF_RAW_MAX);
    CHECK(p.snap.ch[2] == CRSF_RAW_MIN);
    CHECK(p.snap.ch[3] == CRSF_RAW_MAX);
}

static void test_overflow_event(void)
{
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    crsf_parser_t p;
    crsf_parser_init(&p);
    crsf_feed(&p, f, 10, 1);          // неповний кадр у буфері
    crsf_note_overflow(&p);
    CHECK(p.snap.cnt.overflow == 1);
    CHECK(p.n == 0);                  // буфер скинуто
    CHECK(crsf_feed(&p, f, n, 2) == 1);
}

// Детермінований генератор для тесту шуму (фіксоване зерно).
static uint32_t g_rng = 0x1234ABCDu;
static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void test_noise_stream(void)
{
    crsf_parser_t p;
    crsf_parser_init(&p);
    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    size_t injected = 0, accepted = 0, bytes = 0;
    uint8_t chunk[64];
    uint8_t f[CRSF_FRAME_MAX];
    while (bytes < 100000) {
        size_t len = 1 + rnd() % 40;
        for (size_t i = 0; i < len; i++) chunk[i] = (uint8_t)rnd();
        accepted += crsf_feed(&p, chunk, len, (uint32_t)bytes);
        bytes += len;
        if (rnd() % 10 < 3) {
            ch[0] = (uint16_t)(172 + rnd() % 1640);
            size_t n = rc_frame(f, ch);
            accepted += crsf_feed(&p, f, n, (uint32_t)bytes);
            bytes += n;
            injected++;
        }
    }
    // Хибний початок кадру від шуму може тримати буфер до своєї довжини (≤ 62 байти); досилаємо нулі,
    // щоб останній справжній кадр дочитався, і лише тоді дивимось на канали.
    uint8_t zeros[CRSF_FRAME_MAX] = {0};
    accepted += crsf_feed(&p, zeros, sizeof(zeros), (uint32_t)bytes);
    CHECK(p.snap.ch[0] == ch[0]);
    CHECK(bytes >= 100000);
    CHECK(injected > 100);
    CHECK(accepted == injected);                 // 0 хибно прийнятих, жоден валідний не втрачено
    CHECK(p.snap.cnt.rc_ok == injected);
    CHECK(p.snap.seq == injected);
}

// ---------- Намір пілота (T2) ----------

static int feq(float a, float b) { return fabsf(a - b) < 0.0005f; }

static void test_axis_deadband(void)
{
    CHECK(feq(crsf_axis(992, +1), 0.0f));
    CHECK(feq(crsf_axis(982, +1), 0.0f));          // спокій стіка зміщений (≈ 978…1000), мертва зона його поглинає
    CHECK(feq(crsf_axis(1000, +1), 0.0f));
    CHECK(feq(crsf_axis(992 + 40, +1), 0.0f));     // ще в зоні
    CHECK(feq(crsf_axis(992 - 40, +1), 0.0f));
    CHECK(crsf_axis_neutral(992 + 40) && crsf_axis_neutral(992 - 40));
    CHECK(!crsf_axis_neutral(992 + 41) && !crsf_axis_neutral(992 - 41));
    // Неперервність на краю: перший крок за межею дає майже нуль, без стрибка.
    float e = crsf_axis(992 + 41, +1);
    CHECK(e > 0.0f && e < 0.001f);
    e = crsf_axis(992 - 41, +1);
    CHECK(e < 0.0f && e > -0.001f);
    // Кінці шкали.
    CHECK(feq(crsf_axis(CRSF_RAW_MAX, +1), 1.0f));
    CHECK(feq(crsf_axis(CRSF_RAW_MIN, +1), -1.0f));
    CHECK(crsf_axis(1808, +1) > 0.97f && crsf_axis(1808, +1) < 1.0f);   // виміряний упор стіка вгору
    // Лінійність між краєм зони й упором: середина дає ≈ половину.
    float mid = crsf_axis(992 + (41 + 819) / 2, +1);
    CHECK(mid > 0.45f && mid < 0.55f);
    // Знак напрямку.
    CHECK(feq(crsf_axis(CRSF_RAW_MAX, -1), -1.0f));
    CHECK(crsf_axis(1500, +1) > 0.0f && crsf_axis(1500, -1) < 0.0f);
    // Монотонність по всьому діапазону й межі.
    float prev = -2.0f;
    for (int r = 0; r <= 2047; r++) {
        float a = crsf_axis((uint16_t)r, +1);
        CHECK(a >= prev && a >= -1.0f && a <= 1.0f);
        prev = a;
    }
}

static void test_switch(void)
{
    CHECK(!crsf_switch_arm(0));
    CHECK(!crsf_switch_arm(191));
    CHECK(!crsf_switch_arm(992));
    CHECK(crsf_switch_arm(993));
    CHECK(crsf_switch_arm(1792));
}

static void test_tof_control_switch(void)
{
    // CH9: вище середини — «контроль увімкнено» (spec A1); 992 — «вимкнено» (канал без призначеного тумблера)
    CHECK(CRSF_IDX_TOF_CONTROL == 8);
    CHECK(!crsf_tof_control_on(172));
    CHECK(!crsf_tof_control_on(992));
    CHECK(crsf_tof_control_on(993));
    CHECK(crsf_tof_control_on(1811));
    crsf_snapshot_t s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < CRSF_CHANNELS; i++) s.ch[i] = 992;
    // кадру ще не було — за замовчуванням увімкнено
    s.have_frame = false;
    CHECK(crsf_tof_control_effective(&s, true));
    CHECK(crsf_tof_control_effective(&s, false));
    // кадр є, зв'язок живий — слухаємо канал
    s.have_frame = true;
    s.ch[CRSF_IDX_TOF_CONTROL] = 992;
    CHECK(!crsf_tof_control_effective(&s, true));
    s.ch[CRSF_IDX_TOF_CONTROL] = 1792;
    CHECK(crsf_tof_control_effective(&s, true));
    s.ch[CRSF_IDX_TOF_CONTROL] = 172;
    CHECK(!crsf_tof_control_effective(&s, true));
    // зв'язок втрачено — знову за замовчуванням (увімкнено), хоч останнє значення було «вимкнено»
    CHECK(crsf_tof_control_effective(&s, false));
    CHECK(crsf_tof_control_effective(NULL, true));
}

static void test_edge(void)
{
    crsf_switch_t base = CRSF_SW_UNKNOWN;
    // Перший кадр лише встановлює базу, навіть якщо тумблер у ARM.
    CHECK(!crsf_edge(&base, CRSF_SW_ARM));
    CHECK(base == CRSF_SW_ARM);
    CHECK(!crsf_edge(&base, CRSF_SW_ARM));           // тримається в ARM — фронту немає
    CHECK(!crsf_edge(&base, CRSF_SW_DISARM));        // ARM → DISARM — не фронт
    CHECK(base == CRSF_SW_DISARM);
    CHECK(crsf_edge(&base, CRSF_SW_ARM));            // DISARM → ARM
    CHECK(base == CRSF_SW_ARM);
    CHECK(!crsf_edge(&base, CRSF_SW_ARM));           // фронт лише один раз
    // Після втрати зв'язку база невідома: тумблер у DISARM → база DISARM без фронту.
    base = CRSF_SW_UNKNOWN;
    CHECK(!crsf_edge(&base, CRSF_SW_DISARM));
    CHECK(base == CRSF_SW_DISARM);
    CHECK(crsf_edge(&base, CRSF_SW_ARM));
}

static void test_intent(void)
{
    crsf_parser_t p;
    crsf_parser_init(&p);
    crsf_intent_t it;
    crsf_intent(&p.snap, &it);                       // кадрів ще не було
    CHECK(it.throttle_neutral && it.steer_neutral);
    CHECK(feq(it.throttle, 0) && feq(it.steer, 0));
    CHECK(it.sw == CRSF_SW_DISARM);

    uint16_t ch[CRSF_CHANNELS];
    all_centered(ch);
    ch[CRSF_IDX_STEER] = 1792;                       // праворуч (виміряно)
    ch[CRSF_IDX_THROTTLE] = 1808;                    // вгору
    ch[CRSF_IDX_ARM] = 1792;
    uint8_t f[CRSF_FRAME_MAX];
    size_t n = rc_frame(f, ch);
    CHECK(crsf_feed(&p, f, n, 10) == 1);
    crsf_intent(&p.snap, &it);
    CHECK(it.steer > 0.95f && it.throttle > 0.95f);
    CHECK(!it.throttle_neutral && !it.steer_neutral);
    CHECK(it.sw == CRSF_SW_ARM);

    // Спокій зі зміщенням (як виміряно): нейтраль.
    ch[CRSF_IDX_STEER] = 982;
    ch[CRSF_IDX_THROTTLE] = 978;
    ch[CRSF_IDX_ARM] = 191;
    n = rc_frame(f, ch);
    CHECK(crsf_feed(&p, f, n, 20) == 1);
    crsf_intent(&p.snap, &it);
    CHECK(it.throttle_neutral && it.steer_neutral);
    CHECK(feq(it.throttle, 0) && feq(it.steer, 0));
    CHECK(it.sw == CRSF_SW_DISARM);

    // Нуль у кадрі обрізається до нижнього краю, тумблер — DISARM (AC-11).
    ch[CRSF_IDX_ARM] = 0;
    ch[CRSF_IDX_THROTTLE] = 2047;
    n = rc_frame(f, ch);
    CHECK(crsf_feed(&p, f, n, 30) == 1);
    crsf_intent(&p.snap, &it);
    CHECK(it.sw == CRSF_SW_DISARM);
    CHECK(feq(it.throttle, 1.0f));
}

static void test_calib_signs(void)
{
    CHECK(CRSF_THROTTLE_SIGN == +1);
    CHECK(CRSF_STEER_SIGN == +1);
}

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static void test_fmt_status(void)
{
    crsf_status_view_t v;
    memset(&v, 0, sizeof(v));
    char buf[600];

    // Кадрів ще не було, стенд.
    v.source = CRSF_SRC_BENCH;
    v.link = CRSF_LINK_NEVER;
    v.intent.sw = CRSF_SW_UNKNOWN;
    crsf_fmt_status(buf, sizeof(buf), &v);
    CHECK(has(buf, "стенд"));
    CHECK(has(buf, "не було від старту"));
    CHECK(has(buf, "—"));                              // час від кадру і сирі канали — прочерк
    CHECK(has(buf, "невідомо"));                       // тумблер
    CHECK(!has(buf, "причина FAILSAFE"));

    // Пульт, звʼязок живий, тумблер ARM, озброєння заблоковано.
    memset(&v, 0, sizeof(v));
    v.source = CRSF_SRC_REMOTE;
    v.link = CRSF_LINK_ALIVE;
    v.since_last_ms = 12;
    v.snap.have_frame = true;
    v.snap.ch[CRSF_IDX_STEER] = 1006;
    v.snap.ch[CRSF_IDX_THROTTLE] = 1808;
    v.snap.ch[CRSF_IDX_ARM] = 1792;
    v.snap.cnt.rc_ok = 4321;
    v.snap.cnt.crc_fail = 2;
    v.snap.cnt.len_fail = 3;
    v.snap.cnt.other = 77;
    v.snap.cnt.overflow = 1;
    v.intent.throttle = 0.98f;
    v.intent.steer = 0.01f;
    v.intent.sw = CRSF_SW_ARM;
    v.arm_block = "стік не в нейтралі";
    crsf_fmt_status(buf, sizeof(buf), &v);
    CHECK(has(buf, "пульт"));
    CHECK(has(buf, "живий"));
    CHECK(has(buf, "12 мс"));
    CHECK(has(buf, "4321") && has(buf, "77"));
    CHECK(has(buf, "1006") && has(buf, "1808") && has(buf, "1792"));
    CHECK(has(buf, "+0.98"));
    CHECK(has(buf, "ARM"));
    CHECK(has(buf, "стік не в нейтралі"));

    // Стенд закінчено, звʼязок втрачено, FAILSAFE через звʼязок.
    v.source = CRSF_SRC_NONE;
    v.link = CRSF_LINK_LOST;
    v.since_last_ms = 800;
    v.arm_block = NULL;
    v.failsafe_cause = "втрата звʼязку";
    v.intent.sw = CRSF_SW_DISARM;
    crsf_fmt_status(buf, sizeof(buf), &v);
    CHECK(has(buf, "немає"));
    CHECK(has(buf, "втрачено"));
    CHECK(has(buf, "800 мс"));
    CHECK(has(buf, "причина FAILSAFE: втрата звʼязку"));
    CHECK(has(buf, "можна"));                          // озброєння не заблоковано

    // Обрізання буфера не ламає рядок.
    char small[40];
    size_t k = crsf_fmt_status(small, sizeof(small), &v);
    CHECK(k < sizeof(small) && strlen(small) == k);
}

// T15 (review F2): status у FAILSAFE називає, чого бракує для виходу.
static void test_fmt_status_failsafe_exit(void)
{
    crsf_status_view_t v;
    memset(&v, 0, sizeof(v));
    char buf[800];  // як буфер console_hal
    v.source = CRSF_SRC_REMOTE;
    v.link = CRSF_LINK_ALIVE;
    v.failsafe_cause = "втрата звʼязку";
    v.failsafe_exit = "тумблер у ARM: опустіть його в DISARM";
    crsf_fmt_status(buf, sizeof(buf), &v);
    CHECK(has(buf, "вихід з FAILSAFE: тумблер у ARM: опустіть його в DISARM\n"));
    v.failsafe_cause = NULL;
    v.failsafe_exit = NULL;
    crsf_fmt_status(buf, sizeof(buf), &v);
    CHECK(!has(buf, "вихід з FAILSAFE"));
    // Найгірший випадок не обрізається: усі поля найдовші.
    memset(&v, 0, sizeof(v));
    v.source = CRSF_SRC_NONE;
    v.link = CRSF_LINK_LOST;
    v.since_last_ms = 4294967295u;
    v.snap.have_frame = true;
    v.snap.cnt = (crsf_counters_t){4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u};
    for (int i = 0; i < CRSF_CHANNELS; i++) v.snap.ch[i] = 1811;
    v.intent.throttle = -1.0f;
    v.intent.steer = -1.0f;
    v.intent.sw = CRSF_SW_UNKNOWN;
    v.arm_block = "тумблер не перемикався з DISARM: поверніть його в DISARM і знову в ARM";
    v.failsafe_cause = "втрата звʼязку";
    v.failsafe_exit = "після відсічки по напрузі вихід лише скиданням плати чи перемиканням живлення";
    size_t len = crsf_fmt_status(buf, sizeof(buf), &v);
    CHECK(len < sizeof(buf) - 1 && has(buf, "перемиканням живлення\n"));
}

int main(void)
{
    test_fmt_status_failsafe_exit();
    test_crc8();
    test_reference_frame();
    test_unpack_values();
    test_split_chunks();
    test_bad_crc();
    test_resync_inside_bad_frame();
    test_wrong_length();
    test_other_type_does_not_touch_rc();
    test_clamp();
    test_overflow_event();
    test_noise_stream();
    test_axis_deadband();
    test_switch();
    test_tof_control_switch();
    test_edge();
    test_intent();
    test_calib_signs();
    test_fmt_status();
    return test_summary();
}
