// Документ звіту сесії (phase8-session-report, contracts/events.md): див. report_core.h. Без заголовків ESP-IDF.
#include "report_core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *report_end_reason_name(session_end_t r)
{
    switch (r) {
    case SESSION_END_LINK_LOST:
        return "failsafe_link_lost";
    case SESSION_END_VOLTAGE:
        return "failsafe_voltage";
    case SESSION_END_DISARM:
    default:
        return "disarm";
    }
}

const char *report_end_reason_text(session_end_t r)
{
    switch (r) {
    case SESSION_END_LINK_LOST:
        return "втрата зв'язку";
    case SESSION_END_VOLTAGE:
        return "напруга";
    case SESSION_END_DISARM:
    default:
        return "роззброєння";
    }
}

const char *report_sensor_name(int sensor)
{
    static const char *const names[SESSION_SENSORS] = {"left_front", "front", "right_front", "rear"};
    return (sensor >= 0 && sensor < SESSION_SENSORS) ? names[sensor] : NULL;
}

// Складальник: пише в локальний буфер із запасом; переповнення позначається, і документ відкидається.
typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
    bool overflow;
} sb_t;

static void put(sb_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(sb_t *b, const char *fmt, ...)
{
    if (b->overflow) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(b->buf + b->pos, b->cap - b->pos, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= b->cap - b->pos) {
        b->overflow = true;
        return;
    }
    b->pos += (size_t)w;
}

// Рядок JSON з екрануванням ", \ і керуючих символів; обрізаний до max_len байтів (за межею UTF-8 не різати: кириллиці в полях немає).
static void put_str(sb_t *b, const char *s, size_t max_len)
{
    put(b, "\"");
    size_t n = 0;
    while (s != NULL && n < max_len && s[n] != '\0') {
        n++;
    }
    if (n == 0) {
        put(b, "unknown");
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            put(b, "\\%c", c);
        } else if (c < 0x20) {
            put(b, "\\u%04x", c);
        } else {
            put(b, "%c", c);
        }
    }
    put(b, "\"");
}

static void put_mv(sb_t *b, bool valid, uint32_t v)
{
    if (valid) {
        put(b, "%lu", (unsigned long)v);
    } else {
        put(b, "null");
    }
}

static void put_rpm(sb_t *b, bool valid, float v)
{
    if (!valid) {
        put(b, "null");
        return;
    }
    if (fabsf(v) < 0.05f) {
        v = 0.0f;   // без «-0.0»
    }
    put(b, "%.1f", (double)v);
}

size_t report_core_build_json(char *buf, size_t n, const session_summary_t *s, const report_fw_info_t *fw)
{
    if (buf == NULL || n == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (s == NULL) {
        return 0;
    }
    char tmp[REPORT_JSON_MAX + 64];
    sb_t b = {tmp, sizeof(tmp), 0, false};
    put(&b, "{\"event\":\"rover.session.v1\",\"schema_version\":1,\"seq\":%lu,\"duration_ms\":%lu,\"end_reason\":\"%s\",",
        (unsigned long)s->seq, (unsigned long)s->duration_ms, report_end_reason_name(s->end_reason));
    put(&b, "\"voltage_start_mv\":");
    put_mv(&b, s->voltage_start_valid, s->voltage_start_mv);
    put(&b, ",\"voltage_end_mv\":");
    put_mv(&b, s->voltage_end_valid, s->voltage_end_mv);
    put(&b, ",\"voltage_min_mv\":");
    put_mv(&b, s->voltage_min_valid, s->voltage_min_mv);
    put(&b, ",\"holds\":{\"count\":%lu,\"total_ms\":%lu,\"min_mm\":", (unsigned long)s->hold_count, (unsigned long)s->hold_total_ms);
    const char *min_sensor = report_sensor_name(s->hold_min_sensor);
    if (s->hold_min_valid) {
        put(&b, "%u", (unsigned)s->hold_min_mm);
    } else {
        put(&b, "null");
    }
    put(&b, ",\"min_sensor\":");
    if (s->hold_min_valid && min_sensor != NULL) {
        put(&b, "\"%s\"", min_sensor);
    } else {
        put(&b, "null");
    }
    put(&b, ",\"by_sensor\":{");
    for (int i = 0; i < SESSION_SENSORS; i++) {
        put(&b, "%s\"%s\":%lu", i ? "," : "", report_sensor_name(i), (unsigned long)s->hold_by_sensor[i]);
    }
    put(&b, "}},\"tof_control_off_ms\":%lu,\"distance_cm\":", (unsigned long)s->tof_control_off_ms);
    if (s->distance_valid) {
        float d = s->distance_cm < 0.0f ? 0.0f : s->distance_cm;
        put(&b, "%lld", (long long)(d + 0.5f));
    } else {
        put(&b, "null");
    }
    put(&b, ",\"moving_ms\":%lu,\"speed\":{\"left_avg_rpm\":", (unsigned long)s->moving_ms);
    put_rpm(&b, s->left_speed_valid, s->left_avg_rpm);
    put(&b, ",\"left_peak_rpm\":");
    put_rpm(&b, s->left_speed_valid, s->left_peak_rpm);
    put(&b, ",\"right_avg_rpm\":");
    put_rpm(&b, s->right_speed_valid, s->right_avg_rpm);
    put(&b, ",\"right_peak_rpm\":");
    put_rpm(&b, s->right_speed_valid, s->right_peak_rpm);
    put(&b, ",\"imbalance_pct\":");
    put_rpm(&b, s->imbalance_valid, s->imbalance_pct);
    put(&b, "},\"encoders\":{\"left\":%s,\"right\":%s},\"fw\":{\"version\":", s->left_enc_seen ? "true" : "false",
        s->right_enc_seen ? "true" : "false");
    put_str(&b, fw ? fw->version : NULL, REPORT_VERSION_LEN - 1);
    put(&b, ",\"git\":");
    put_str(&b, fw ? fw->git : NULL, REPORT_GIT_LEN - 1);
    if (fw != NULL && fw->dirty_known) {
        put(&b, ",\"dirty\":%s", fw->dirty ? "true" : "false");
    }
    put(&b, ",\"idf\":");
    put_str(&b, fw ? fw->idf : NULL, REPORT_VERSION_LEN - 1);
    put(&b, "},\"mac\":");
    put_str(&b, fw ? fw->mac : NULL, REPORT_MAC_LEN - 1);
    put(&b, "}");
    if (b.overflow || b.pos > REPORT_JSON_MAX || b.pos + 1 > n) {
        return 0;
    }
    memcpy(buf, tmp, b.pos + 1);
    return b.pos;
}

static bool all_hex(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return n > 0;
}

void report_fw_from_version(report_fw_info_t *fw, const char *version)
{
    if (fw == NULL) {
        return;
    }
    fw->version[0] = '\0';
    fw->git[0] = '\0';
    fw->dirty_known = false;
    fw->dirty = false;
    if (version == NULL) {
        return;
    }
    size_t full = strlen(version);
    size_t keep = full < REPORT_VERSION_LEN - 1 ? full : REPORT_VERSION_LEN - 1;
    memcpy(fw->version, version, keep);
    fw->version[keep] = '\0';
    // Суфікс «-dirty»; решта — хеш або `<тег>-<N>-g<хеш>`.
    size_t len = full;
    bool dirty = false;
    static const char suffix[] = "-dirty";
    if (len > sizeof(suffix) - 1 && strcmp(version + len - (sizeof(suffix) - 1), suffix) == 0) {
        dirty = true;
        len -= sizeof(suffix) - 1;
    }
    const char *hash = NULL;
    size_t hash_len = 0;
    if (len >= 7 && len < REPORT_GIT_LEN && all_hex(version, len)) {
        hash = version;
        hash_len = len;
    } else {
        for (size_t i = len; i-- > 1;) {
            if (version[i] == 'g' && version[i - 1] == '-') {
                size_t hl = len - (i + 1);
                if (hl >= 7 && hl < REPORT_GIT_LEN && all_hex(version + i + 1, hl)) {
                    hash = version + i + 1;
                    hash_len = hl;
                }
                break;
            }
        }
    }
    if (hash != NULL) {
        memcpy(fw->git, hash, hash_len);
        fw->git[hash_len] = '\0';
        fw->dirty_known = true;
        fw->dirty = dirty;
    }
}
