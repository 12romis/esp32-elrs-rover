// Хостові тести report_core (phase8-session-report T7; spec A11, A12, AC-11, AC-21, AC-25): компактний JSON звіту.
#include <string.h>

#include "report_core.h"
#include "test_support.h"

static report_fw_info_t fw_ok(void)
{
    report_fw_info_t fw;
    memset(&fw, 0, sizeof(fw));
    strcpy(fw.version, "x.y.z");
    strcpy(fw.git, "abcdef0");
    strcpy(fw.idf, "v6.1");
    strcpy(fw.mac, "aabbccddeeff");
    return fw;
}

// Повний звіт з прикладу events.md.
static session_summary_t full(void)
{
    session_summary_t s;
    memset(&s, 0, sizeof(s));
    s.seq = 42;
    s.duration_ms = 187000;
    s.end_reason = SESSION_END_DISARM;
    s.voltage_start_valid = s.voltage_end_valid = s.voltage_min_valid = true;
    s.voltage_start_mv = 8240;
    s.voltage_end_mv = 7710;
    s.voltage_min_mv = 7480;
    s.hold_count = 3;
    s.hold_total_ms = 4200;
    s.hold_min_valid = true;
    s.hold_min_mm = 95;
    s.hold_min_sensor = 1;
    s.hold_by_sensor[0] = 1;
    s.hold_by_sensor[1] = 3;
    s.hold_by_sensor[2] = 2;
    s.hold_by_sensor[3] = 0;
    s.tof_control_off_ms = 0;
    s.left_enc_seen = s.right_enc_seen = true;
    s.distance_valid = true;
    s.distance_cm = 5320.0f;
    s.moving_ms = 142000;
    s.left_speed_valid = s.right_speed_valid = true;
    s.left_avg_rpm = 88.4f;
    s.left_peak_rpm = 141.0f;
    s.right_avg_rpm = 84.1f;
    s.right_peak_rpm = 138.5f;
    s.imbalance_valid = true;
    s.imbalance_pct = 5.0f;
    return s;
}

static void test_full_report_matches_example(void)
{
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s = full();
    report_fw_info_t fw = fw_ok();
    size_t n = report_core_build_json(buf, sizeof(buf), &s, &fw);
    const char *want =
        "{\"event\":\"rover.session.v1\",\"schema_version\":1,\"seq\":42,\"duration_ms\":187000,\"end_reason\":\"disarm\","
        "\"voltage_start_mv\":8240,\"voltage_end_mv\":7710,\"voltage_min_mv\":7480,"
        "\"holds\":{\"count\":3,\"total_ms\":4200,\"min_mm\":95,\"min_sensor\":\"front\","
        "\"by_sensor\":{\"left_front\":1,\"front\":3,\"right_front\":2,\"rear\":0}},"
        "\"tof_control_off_ms\":0,\"distance_cm\":5320,\"moving_ms\":142000,"
        "\"speed\":{\"left_avg_rpm\":88.4,\"left_peak_rpm\":141.0,\"right_avg_rpm\":84.1,\"right_peak_rpm\":138.5,"
        "\"imbalance_pct\":5.0},"
        "\"encoders\":{\"left\":true,\"right\":true},"
        "\"fw\":{\"version\":\"x.y.z\",\"git\":\"abcdef0\",\"idf\":\"v6.1\"},\"mac\":\"aabbccddeeff\"}";
    CHECK(n == strlen(want));
    CHECK(strcmp(buf, want) == 0);
    CHECK(n <= REPORT_JSON_MAX);
}

static void test_missing_data_example(void)
{
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s;
    memset(&s, 0, sizeof(s));
    s.seq = 43;
    s.duration_ms = 9000;
    s.end_reason = SESSION_END_VOLTAGE;
    s.voltage_start_valid = s.voltage_end_valid = s.voltage_min_valid = true;
    s.voltage_start_mv = 6100;
    s.voltage_end_mv = 5950;
    s.voltage_min_mv = 5950;
    s.hold_min_sensor = -1;
    s.distance_valid = false;                    // жодного доступного енкодера
    report_fw_info_t fw = fw_ok();
    size_t n = report_core_build_json(buf, sizeof(buf), &s, &fw);
    const char *want =
        "{\"event\":\"rover.session.v1\",\"schema_version\":1,\"seq\":43,\"duration_ms\":9000,\"end_reason\":\"failsafe_voltage\","
        "\"voltage_start_mv\":6100,\"voltage_end_mv\":5950,\"voltage_min_mv\":5950,"
        "\"holds\":{\"count\":0,\"total_ms\":0,\"min_mm\":null,\"min_sensor\":null,"
        "\"by_sensor\":{\"left_front\":0,\"front\":0,\"right_front\":0,\"rear\":0}},"
        "\"tof_control_off_ms\":0,\"distance_cm\":null,\"moving_ms\":0,"
        "\"speed\":{\"left_avg_rpm\":null,\"left_peak_rpm\":null,\"right_avg_rpm\":null,\"right_peak_rpm\":null,"
        "\"imbalance_pct\":null},"
        "\"encoders\":{\"left\":false,\"right\":false},"
        "\"fw\":{\"version\":\"x.y.z\",\"git\":\"abcdef0\",\"idf\":\"v6.1\"},\"mac\":\"aabbccddeeff\"}";
    CHECK(n == strlen(want) && strcmp(buf, want) == 0);
}

static void test_all_null(void)
{
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s;
    memset(&s, 0, sizeof(s));
    s.hold_min_sensor = -1;
    report_fw_info_t fw = fw_ok();
    size_t n = report_core_build_json(buf, sizeof(buf), &s, &fw);
    CHECK(n > 0);
    CHECK(strstr(buf, "\"voltage_start_mv\":null") && strstr(buf, "\"voltage_end_mv\":null") && strstr(buf, "\"voltage_min_mv\":null"));
    CHECK(strstr(buf, "\"left_avg_rpm\":null") && strstr(buf, "\"imbalance_pct\":null") && strstr(buf, "\"distance_cm\":null"));
    CHECK(strstr(buf, "\"min_mm\":null") && strstr(buf, "\"min_sensor\":null"));
    CHECK(strstr(buf, "\"moving_ms\":0"));                                // виміряний нуль залишається нулем
}

static void test_one_board_missing(void)
{
    // AC-21: правий борт без енкодера → його швидкості й дисбаланс null, лівий є
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s = full();
    s.right_speed_valid = false;
    s.right_enc_seen = false;
    s.imbalance_valid = false;
    report_fw_info_t fw = fw_ok();
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0);
    CHECK(strstr(buf, "\"left_avg_rpm\":88.4") && strstr(buf, "\"right_avg_rpm\":null") && strstr(buf, "\"right_peak_rpm\":null"));
    CHECK(strstr(buf, "\"imbalance_pct\":null"));
    CHECK(strstr(buf, "\"encoders\":{\"left\":true,\"right\":false}") != NULL);    // AC-21: звіт пояснює, чому null
}

static void test_end_reasons_and_negative_imbalance(void)
{
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s = full();
    s.end_reason = SESSION_END_LINK_LOST;
    s.imbalance_pct = -12.34f;
    report_fw_info_t fw = fw_ok();
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0);
    CHECK(strstr(buf, "\"end_reason\":\"failsafe_link_lost\"") && strstr(buf, "\"imbalance_pct\":-12.3"));
    CHECK(strcmp(report_end_reason_name(SESSION_END_DISARM), "disarm") == 0);
    CHECK(strcmp(report_end_reason_name(SESSION_END_VOLTAGE), "failsafe_voltage") == 0);
    CHECK(strcmp(report_sensor_name(0), "left_front") == 0 && strcmp(report_sensor_name(3), "rear") == 0);
    CHECK(report_sensor_name(4) == NULL && report_sensor_name(-1) == NULL);
}

static void test_fw_dirty(void)
{
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s = full();
    report_fw_info_t fw = fw_ok();
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0 && strstr(buf, "dirty") == NULL);   // не вигадується
    fw.dirty_known = true;
    fw.dirty = false;
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0 && strstr(buf, "\"dirty\":false") != NULL);
    fw.dirty = true;
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0 && strstr(buf, "\"dirty\":true") != NULL);
}

static void test_escaping_and_truncation(void)
{
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s = full();
    report_fw_info_t fw = fw_ok();
    strcpy(fw.version, "a\"b\\c\n");
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0);
    CHECK(strstr(buf, "\"version\":\"a\\\"b\\\\c\\u000a\"") != NULL);
    // порожня й відсутня версія — «unknown» (events.md)
    fw.version[0] = '\0';
    fw.git[0] = '\0';
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0);
    CHECK(strstr(buf, "\"version\":\"unknown\"") && strstr(buf, "\"git\":\"unknown\""));
    CHECK(report_core_build_json(buf, sizeof(buf), &s, NULL) > 0 && strstr(buf, "\"mac\":\"unknown\""));
    // найдовші значення: документ усе одно ≤ 1024
    memset(fw.version, 'v', sizeof(fw.version) - 1);
    fw.version[sizeof(fw.version) - 1] = '\0';
    memset(fw.git, 'g', sizeof(fw.git) - 1);
    fw.git[sizeof(fw.git) - 1] = '\0';
    memset(fw.idf, 'i', sizeof(fw.idf) - 1);
    fw.idf[sizeof(fw.idf) - 1] = '\0';
    fw.dirty_known = true;
    s.seq = 4294967295u;
    s.duration_ms = 4294967295u;
    s.voltage_start_mv = s.voltage_end_mv = s.voltage_min_mv = 4294967295u;
    s.hold_count = s.hold_total_ms = 4294967295u;
    for (int i = 0; i < 4; i++) s.hold_by_sensor[i] = 4294967295u;
    s.tof_control_off_ms = s.moving_ms = 4294967295u;
    s.distance_cm = 99999999.0f;
    s.left_avg_rpm = s.left_peak_rpm = s.right_avg_rpm = s.right_peak_rpm = 99999.9f;
    s.imbalance_pct = -999.9f;
    size_t n = report_core_build_json(buf, sizeof(buf), &s, &fw);
    CHECK(n > 0 && n <= REPORT_JSON_MAX && n == strlen(buf));
}

static void test_small_buffer(void)
{
    session_summary_t s = full();
    report_fw_info_t fw = fw_ok();
    char guard[64];
    memset(guard, 'Z', sizeof(guard));
    char small[100];
    memset(small, 'Q', sizeof(small));
    CHECK(report_core_build_json(small, 50, &s, &fw) == 0);
    CHECK(small[0] == '\0' && small[50] == 'Q');                         // за межі буфера не вийшло
    CHECK(report_core_build_json(NULL, 0, &s, &fw) == 0);
    char buf[REPORT_JSON_MAX + 1];
    CHECK(report_core_build_json(buf, sizeof(buf), NULL, &fw) == 0);
    // буфер рівно на довжину без нуля — відмова (місця для завершального нуля немає)
    size_t need = report_core_build_json(buf, sizeof(buf), &s, &fw);
    char exact[REPORT_JSON_MAX + 1];
    CHECK(report_core_build_json(exact, need, &s, &fw) == 0);
    CHECK(report_core_build_json(exact, need + 1, &s, &fw) == need);
}

static void test_fw_from_version(void)
{
    report_fw_info_t fw;
    memset(&fw, 0, sizeof(fw));
    report_fw_from_version(&fw, "b667905");                       // git describe --always у чистому дереві
    CHECK(strcmp(fw.version, "b667905") == 0 && strcmp(fw.git, "b667905") == 0 && fw.dirty_known && !fw.dirty);
    report_fw_from_version(&fw, "b667905-dirty");
    CHECK(strcmp(fw.version, "b667905-dirty") == 0 && strcmp(fw.git, "b667905") == 0 && fw.dirty_known && fw.dirty);
    report_fw_from_version(&fw, "v1.2-3-gabcdef0");
    CHECK(strcmp(fw.git, "abcdef0") == 0 && fw.dirty_known && !fw.dirty);
    report_fw_from_version(&fw, "v1.2-3-gabcdef0-dirty");
    CHECK(strcmp(fw.git, "abcdef0") == 0 && fw.dirty_known && fw.dirty);
    report_fw_from_version(&fw, "1");                             // збірка без git: нічого не вигадується
    CHECK(strcmp(fw.version, "1") == 0 && fw.git[0] == '\0' && !fw.dirty_known);
    report_fw_from_version(&fw, "v2.0");
    CHECK(fw.git[0] == '\0' && !fw.dirty_known);
    report_fw_from_version(&fw, NULL);
    CHECK(fw.version[0] == '\0' && fw.git[0] == '\0' && !fw.dirty_known);
    char longv[100];
    memset(longv, 'a', sizeof(longv) - 1);
    longv[sizeof(longv) - 1] = '\0';
    report_fw_from_version(&fw, longv);                            // довга версія обрізається в межах буфера
    CHECK(strlen(fw.version) == REPORT_VERSION_LEN - 1);
    // у документі: git-хеш і dirty з версії
    char buf[REPORT_JSON_MAX + 1];
    session_summary_t s = full();
    strcpy(fw.idf, "v6.1");
    strcpy(fw.mac, "aabbccddeeff");
    report_fw_from_version(&fw, "b667905-dirty");
    CHECK(report_core_build_json(buf, sizeof(buf), &s, &fw) > 0);
    CHECK(strstr(buf, "\"git\":\"b667905\",\"dirty\":true,\"idf\"") != NULL);
}

int main(void)
{
    test_fw_from_version();
    test_full_report_matches_example();
    test_missing_data_example();
    test_all_null();
    test_one_board_missing();
    test_end_reasons_and_negative_imbalance();
    test_fw_dirty();
    test_escaping_and_truncation();
    test_small_buffer();
    return test_summary();
}
