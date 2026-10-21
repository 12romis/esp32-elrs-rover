// Хостові тести telemetry_core (phase7-telemetry AC-01, AC-02, AC-04, AC-05, AC-19): кадр датчика батареї,
// рішення «слати чи ні», детектор зміни збою, статистика часу кроку керування.
#include <stdint.h>
#include <string.h>

#include "battery_calib.h"
#include "crsf_core.h"
#include "telemetry_core.h"
#include "test_support.h"

static void test_frame_layout(void)
{
    uint8_t f[TELEMETRY_BATTERY_FRAME_LEN + 4];
    memset(f, 0xAA, sizeof(f));
    CHECK(telemetry_core_build_battery_frame(7400, f, sizeof(f)) == TELEMETRY_BATTERY_FRAME_LEN);
    CHECK(f[0] == CRSF_SYNC_BYTE);
    CHECK(f[1] == 10);                       // тип(1) + 8 байтів + CRC(1)
    CHECK(f[2] == TELEMETRY_TYPE_BATTERY);
    CHECK(f[3] == 0 && f[4] == 74);          // 7.4 В = 74 десятки, старший байт першим
    for (int i = 5; i <= 10; i++) CHECK(f[i] == 0);   // струм, ємність, відсоток заряду — «немає даних»
    CHECK(f[11] == crsf_crc8(&f[2], 9));     // CRC8 по типу й корисному навантаженню
    CHECK(f[12] == 0xAA);                    // за межами кадру нічого не записано
}

static void test_rounding_and_limits(void)
{
    uint8_t f[TELEMETRY_BATTERY_FRAME_LEN];
    telemetry_core_build_battery_frame(7449, f, sizeof f);
    CHECK(f[3] == 0 && f[4] == 74);          // 7449 мВ → 7.4 В
    telemetry_core_build_battery_frame(7450, f, sizeof f);
    CHECK(f[4] == 75);                       // 7450 мВ → до найближчого
    telemetry_core_build_battery_frame(6000, f, sizeof f);
    CHECK(f[4] == 60);
    telemetry_core_build_battery_frame(0, f, sizeof f);
    CHECK(f[3] == 0 && f[4] == 0);
    telemetry_core_build_battery_frame(4000000000u, f, sizeof f);   // не переповнюється: стеля 0x7FFF
    CHECK(f[3] == 0x7F && f[4] == 0xFF);
    CHECK(telemetry_core_build_battery_frame(7400, f, TELEMETRY_BATTERY_FRAME_LEN - 1) == 0);   // замалий буфер
    CHECK(telemetry_core_build_battery_frame(7400, NULL, 64) == 0);
}

static void test_should_send(void)
{
    battery_snapshot_t s = {.mv = 7400, .t_ms = 1000, .valid = true};
    CHECK(telemetry_core_should_send(&s, 1000));
    CHECK(telemetry_core_should_send(&s, 1000 + BATTERY_STALE_MS));
    CHECK(!telemetry_core_should_send(&s, 1000 + BATTERY_STALE_MS + 1));    // застарів
    s.valid = false;
    CHECK(!telemetry_core_should_send(&s, 1000));                           // ще не міряли
    CHECK(!telemetry_core_should_send(NULL, 1000));
}

static void test_change_detector(void)
{
    tel_link_t l;
    telemetry_core_link_init(&l);
    CHECK(telemetry_core_link_report(&l, true) == TEL_EVT_NONE);        // перший успіх — не подія
    CHECK(telemetry_core_link_report(&l, true) == TEL_EVT_NONE);
    CHECK(telemetry_core_link_report(&l, false) == TEL_EVT_FAILED);     // зміна
    CHECK(telemetry_core_link_report(&l, false) == TEL_EVT_NONE);       // без повтору на кожен період
    CHECK(telemetry_core_link_report(&l, false) == TEL_EVT_NONE);
    CHECK(telemetry_core_link_report(&l, true) == TEL_EVT_RESTORED);
    telemetry_core_link_init(&l);
    CHECK(telemetry_core_link_report(&l, false) == TEL_EVT_FAILED);     // перший збій — подія
}

static void test_decide_one_event_per_change(void)
{
    tel_link_t l;
    tel_event_t ev;
    battery_snapshot_t bad = {.valid = false};
    battery_snapshot_t good = {.mv = 7400, .t_ms = 1000, .valid = true};
    telemetry_core_link_init(&l);
    CHECK(!telemetry_core_decide(&l, &bad, 1000, &ev) && ev == TEL_EVT_FAILED);      // ще не міряли: одне попередження
    CHECK(!telemetry_core_decide(&l, &bad, 1200, &ev) && ev == TEL_EVT_NONE);        // без повтору на кожен період
    CHECK(!telemetry_core_decide(&l, &bad, 1400, &ev) && ev == TEL_EVT_NONE);
    CHECK(telemetry_core_decide(&l, &good, 1400, &ev) && ev == TEL_EVT_RESTORED);
    CHECK(telemetry_core_decide(&l, &good, 1600, &ev) && ev == TEL_EVT_NONE);
    CHECK(!telemetry_core_decide(&l, &good, 1000 + BATTERY_STALE_MS + 1, &ev) && ev == TEL_EVT_FAILED);   // застарів
    CHECK(!telemetry_core_decide(&l, NULL, 0, NULL));                                // не падає
}

static void test_step_stats(void)
{
    tel_step_stats_t s;
    char buf[256];   // українські літери — по 2 байти
    telemetry_core_step_init(&s);
    telemetry_core_step_text(&s, 20000, buf, sizeof buf);
    CHECK(strstr(buf, "вимірів ще немає") != NULL);                     // AC-05: не нулі
    CHECK(strstr(buf, " 0 ") == NULL);
    telemetry_core_step_record(&s, 300, 20000);
    CHECK(s.count == 1 && s.last_us == 300 && s.max_us == 300 && s.over == 0);
    telemetry_core_step_record(&s, 900, 20000);
    telemetry_core_step_record(&s, 600, 20000);
    CHECK(s.count == 3 && s.last_us == 600 && s.max_us == 900);
    telemetry_core_step_text(&s, 20000, buf, sizeof buf);
    CHECK(strstr(buf, "останній 600 мкс") && strstr(buf, "середній 600 мкс") && strstr(buf, "найбільший 900 мкс"));
    CHECK(strstr(buf, "період 20000 мкс") && strstr(buf, "перевищень 0 з 3"));
    telemetry_core_step_record(&s, 20001, 20000);                       // понад період
    telemetry_core_step_record(&s, 20000, 20000);                       // рівно період — ще не перевищення
    CHECK(s.over == 1 && s.count == 5);
    telemetry_core_step_text(&s, 20000, buf, sizeof buf);
    CHECK(strstr(buf, "перевищень 1 з 5"));
}

static void test_step_stats_robust(void)
{
    tel_step_stats_t s;
    char buf[8];
    telemetry_core_step_init(&s);
    for (int i = 0; i < 100000; i++) telemetry_core_step_record(&s, 4000000000u, 20000);   // сума не переповнюється
    CHECK(s.count == 100000 && s.max_us == 4000000000u);
    telemetry_core_step_text(&s, 20000, buf, sizeof buf);               // малий буфер: обрізання, завжди нуль-термінатор
    CHECK(strlen(buf) < sizeof buf);
    CHECK(telemetry_core_step_text(NULL, 20000, buf, sizeof buf) == 0);
    telemetry_core_step_record(NULL, 1, 1);                             // не падає
}

int main(void)
{
    test_frame_layout();
    test_rounding_and_limits();
    test_should_send();
    test_change_detector();
    test_decide_one_event_per_change();
    test_step_stats();
    test_step_stats_robust();
    return test_summary();
}
