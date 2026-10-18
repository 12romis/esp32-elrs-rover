// Хостові тести crsf_core (spec AC-06, AC-10, AC-11; sad.md §4): збирач кадрів, CRC8, розпакування, лічильники.
#include <stdint.h>
#include <string.h>

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

int main(void)
{
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
    return test_summary();
}
