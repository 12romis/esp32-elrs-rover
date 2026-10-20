// Чиста логіка регулятора швидкості бортів (phase5-pid; ADR-0001, ADR-0002). Без заголовків ESP-IDF і FreeRTOS.
#include "pid_core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ---------- Набір коефіцієнтів ----------

static const char *const PARAM_NAME[PID_PARAM_COUNT] = { "kp", "ki", "kd", "ramp" };

void pid_set_init(pid_set_t *s)
{
    memset(s, 0, sizeof *s);
    s->mode = PID_MODE_UNCONFIGURED;
    s->saved = false;
}

bool pid_set_complete(const pid_set_t *s)
{
    for (int i = 0; i < PID_PARAM_COUNT; i++) {
        if (!s->set[i]) return false;
    }
    return true;
}

const char *pid_param_name(pid_param_t p)
{
    return (unsigned)p < PID_PARAM_COUNT ? PARAM_NAME[p] : "?";
}

bool pid_param_parse(const char *name, size_t len, pid_param_t *out)
{
    for (int i = 0; i < PID_PARAM_COUNT; i++) {
        if (strlen(PARAM_NAME[i]) == len && strncmp(name, PARAM_NAME[i], len) == 0) {
            *out = (pid_param_t)i;
            return true;
        }
    }
    return false;
}

const char *pid_value_check(pid_param_t p, float value)
{
    if (!isfinite(value)) return "число має бути скінченним";
    if (p == PID_PARAM_RAMP) {
        return value > 0.0f ? NULL : "темп розгону має бути додатним";
    }
    return value >= 0.0f ? NULL : "коефіцієнт має бути невідʼємним";
}

const char *pid_set_param(pid_set_t *s, pid_param_t p, float value)
{
    if ((unsigned)p >= PID_PARAM_COUNT) return "невідоме число набору";
    const char *why = pid_value_check(p, value);
    if (why) return why;
    s->v[p] = value;
    s->set[p] = true;
    s->saved = false;
    if (s->mode == PID_MODE_UNCONFIGURED && pid_set_complete(s)) s->mode = PID_MODE_OFF;
    return NULL;
}

const char *pid_set_mode(pid_set_t *s, bool on)
{
    if (!pid_set_complete(s)) return "набір неповний: спершу задайте всі чотири числа";
    pid_mode_t m = on ? PID_MODE_ON : PID_MODE_OFF;
    if (s->mode != m) {
        s->mode = m;
        s->saved = false;
    }
    return NULL;
}

const char *pid_mode_change(pid_set_t *s, bool on, bool disarmed)
{
    // Стан перевіряється тут, разом із записом режиму, а не лише при розборі команди (spec AC-15, рев'ю N4).
    if (!disarmed) return "режим регулятора перемикається лише в DISARMED";
    return pid_set_mode(s, on);
}

size_t pid_set_missing(const pid_set_t *s, char *buf, size_t n)
{
    size_t len = 0;
    if (n == 0) return 0;
    buf[0] = 0;
    for (int i = 0; i < PID_PARAM_COUNT; i++) {
        if (s->set[i]) continue;
        int w = snprintf(buf + len, n - len, "%s%s", len ? ", " : "", PARAM_NAME[i]);
        if (w < 0) break;
        len += (size_t)w;
        if (len >= n) {
            len = n - 1;
            break;
        }
    }
    return len;
}

// ---------- Запис набору (ADR-0002) ----------

uint32_t pid_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    put_u32(p, v);
}

static float get_f32(const uint8_t *p)
{
    uint32_t v = get_u32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

// Розкладка: 0 магія, 4 версія, 5 режим, 6–7 резерв, 8 kp, 12 ki, 16 kd, 20 ramp, 24 CRC32.
#define OFF_VERSION 4
#define OFF_MODE 5
#define OFF_VALUES 8
#define OFF_CRC 24

size_t pid_record_encode(const pid_set_t *s, uint8_t out[PID_RECORD_SIZE])
{
    if (!pid_set_complete(s)) return 0;
    memset(out, 0, PID_RECORD_SIZE);
    put_u32(out, PID_RECORD_MAGIC);
    out[OFF_VERSION] = (uint8_t)PID_RECORD_VERSION;
    out[OFF_MODE] = (uint8_t)(s->mode == PID_MODE_ON ? PID_MODE_ON : PID_MODE_OFF);
    for (int i = 0; i < PID_PARAM_COUNT; i++) put_f32(out + OFF_VALUES + 4 * i, s->v[i]);
    put_u32(out + OFF_CRC, pid_crc32(out, OFF_CRC));
    return PID_RECORD_SIZE;
}

pid_rec_result_t pid_record_decode(const uint8_t *data, size_t len, pid_set_t *out)
{
    if (data == NULL || len == 0) return PID_REC_ABSENT;
    if (len != PID_RECORD_SIZE) return PID_REC_SIZE;
    if (get_u32(data) != PID_RECORD_MAGIC) return PID_REC_MAGIC;
    if (pid_crc32(data, OFF_CRC) != get_u32(data + OFF_CRC)) return PID_REC_CRC;
    if (data[OFF_VERSION] != PID_RECORD_VERSION) return PID_REC_VERSION;

    pid_set_t s;
    pid_set_init(&s);
    for (int i = 0; i < PID_PARAM_COUNT; i++) {
        float v = get_f32(data + OFF_VALUES + 4 * i);
        if (pid_value_check((pid_param_t)i, v) != NULL) return PID_REC_VALUE;
        s.v[i] = v;
        s.set[i] = true;
    }
    if (data[OFF_MODE] == PID_MODE_ON) {
        s.mode = PID_MODE_ON;
    } else if (data[OFF_MODE] == PID_MODE_OFF) {
        s.mode = PID_MODE_OFF;
    } else {
        return PID_REC_VALUE;
    }
    s.saved = true;
    *out = s;
    return PID_REC_OK;
}

const char *pid_record_result_text(pid_rec_result_t r)
{
    switch (r) {
    case PID_REC_OK: return "набір завантажено";
    case PID_REC_ABSENT: return "запис відсутній";
    case PID_REC_SIZE: return "запис неповний або чужий за розміром";
    case PID_REC_MAGIC: return "чужий запис";
    case PID_REC_VERSION: return "інша версія формату запису";
    case PID_REC_CRC: return "запис пошкоджено (контрольна сума не збігається)";
    case PID_REC_VALUE: return "запис містить недопустиме число чи режим";
    case PID_REC_READ: return "запис не прочитано (збій сховища)";
    }
    return "невідомий результат";
}

// ---------- Взаємне виключення озброєння, запису набору і зміни режиму (spec AC-15) ----------

void pid_guard_init(pid_guard_t *g)
{
    g->saving = false;
    g->changing = false;
    g->arming = false;
}

bool pid_guard_try_save(pid_guard_t *g)
{
    if (g->saving || g->changing || g->arming) return false;
    g->saving = true;
    return true;
}

void pid_guard_end_save(pid_guard_t *g)
{
    g->saving = false;
}

bool pid_guard_try_mode(pid_guard_t *g)
{
    if (g->saving || g->changing || g->arming) return false;
    g->changing = true;
    return true;
}

void pid_guard_end_mode(pid_guard_t *g)
{
    g->changing = false;
}

bool pid_guard_try_arm(pid_guard_t *g, pid_busy_t *why)
{
    *why = g->saving ? PID_BUSY_SAVE : (g->changing ? PID_BUSY_MODE : PID_BUSY_NONE);
    if (*why != PID_BUSY_NONE) return false;
    g->arming = true;
    return true;
}

void pid_guard_end_arm(pid_guard_t *g)
{
    g->arming = false;
}

// ---------- Крок регулятора (spec «Поведінка регулятора»; sad.md §4) ----------

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int8_t sign_of(float v)
{
    return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0);
}

void pid_side_reset(pid_side_t *st)
{
    memset(st, 0, sizeof *st);
}

float pid_target_from_mix(float mix)
{
    return clampf(mix, -1.0f, 1.0f) * PID_TARGET_MAX_RPM;
}

float pid_ramp(float current_rpm, float requested_rpm, float ramp_rpm_s, float dt_s)
{
    float step = ramp_rpm_s * dt_s;
    int8_t req_dir = sign_of(requested_rpm);
    if (req_dir == 0) return 0.0f;
    // Зміна знака проходить через нуль одразу: розгін у новому напрямку стартує з нуля.
    float cur = sign_of(current_rpm) == req_dir ? fabsf(current_rpm) : 0.0f;
    float req = fabsf(requested_rpm);
    float next = req <= cur ? req : (req - cur <= step ? req : cur + step);   // зменшення — одразу
    return (float)req_dir * next;
}

pid_src_t pid_side_speed(bool avail_a, float rpm_a, bool avail_b, float rpm_b, float *rpm)
{
    if (avail_a && avail_b) {
        *rpm = (rpm_a + rpm_b) / 2.0f;
        return PID_SRC_TWO;
    }
    if (avail_a || avail_b) {
        *rpm = avail_a ? rpm_a : rpm_b;
        return PID_SRC_ONE;
    }
    *rpm = 0.0f;
    return PID_SRC_NONE;
}

float pid_side_step(pid_side_t *st, const pid_set_t *set, float target_rpm, float speed_rpm, float dt_s, float limit)
{
    int8_t dir = sign_of(target_rpm);
    if (dir == 0 || (st->dir != 0 && st->dir != dir)) pid_side_reset(st);   // нульова ціль чи зміна напрямку
    if (dir == 0) return 0.0f;

    float dt = clampf(dt_s, PID_DT_MIN_S, PID_DT_MAX_S);
    float lim = clampf(limit, 0.0f, 1.0f);
    // Усе — у системі напрямку цілі: додатна похибка = потрібно більше потужності в напрямку цілі.
    float e = (float)dir * (target_rpm - speed_rpm);
    float p = set->v[PID_PARAM_KP] * e;
    float d = st->has_prev ? -set->v[PID_PARAM_KD] * (float)dir * (speed_rpm - st->prev_speed) / dt : 0.0f;   // від виміру
    float cand = st->integ + set->v[PID_PARAM_KI] * e * dt;   // накопичується Ki·e·dt: зміна Ki без стрибка
    float raw = p + cand + d;
    // Накопичення не росте в бік насичення: на межі не росте вгору, на нулі не росте вниз; у протилежний бік — може.
    // Біля межі накопичене доростає лише до того, що дає рівно межу (spec AC-10: вихід «стоїть на межі»).
    float next = cand;
    if (raw > lim && e > 0.0f) {
        float edge = lim - p - d;
        next = edge > st->integ ? (edge < cand ? edge : cand) : st->integ;
    } else if (raw < 0.0f && e < 0.0f) {
        float edge = -p - d;
        next = edge < st->integ ? (edge > cand ? edge : cand) : st->integ;
    }
    st->integ = clampf(next, 0.0f, lim);

    float u = clampf(p + st->integ + d, 0.0f, lim);   // від нуля до межі в напрямку цілі, без зворотної потужності
    st->prev_speed = speed_rpm;
    st->has_prev = true;
    st->dir = dir;
    st->out = (float)dir * u;
    return st->out;
}

// ---------- Показ стану і трасування (spec AC-03, AC-05; ADR-0003) ----------

// Дописує форматований текст у буфер з обрізанням; *len не виходить за n − 1.
static void appendf(char *buf, size_t n, size_t *len, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static void appendf(char *buf, size_t n, size_t *len, const char *fmt, ...)
{
    if (n == 0 || *len >= n - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *len, n - *len, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    *len += (size_t)w;
    if (*len > n - 1) *len = n - 1;
}

void pid_trace_acc_reset(pid_trace_acc_t *a)
{
    memset(a, 0, sizeof *a);
}

bool pid_trace_acc_add(pid_trace_acc_t *a, const pid_side_view_t side[2], uint32_t t_ms, bool armed,
                       pid_trace_frame_t *frame)
{
    for (int b = 0; b < 2; b++) {
        a->sum_target[b] += side[b].target_rpm;
        a->sum_speed[b] += side[b].speed_rpm;
        a->sum_out[b] += side[b].out;
        a->sum_mix[b] += side[b].mix;
    }
    if (++a->n < PID_TRACE_TICKS) return false;

    float k = 1.0f / (float)a->n;
    memset(frame, 0, sizeof *frame);
    frame->t_ms = t_ms;
    frame->seq = ++a->seq;
    frame->armed = armed;
    for (int b = 0; b < 2; b++) {
        frame->mode[b] = side[b].mode;
        frame->missing[b] = side[b].missing;
        frame->target_rpm[b] = a->sum_target[b] * k;
        frame->speed_rpm[b] = a->sum_speed[b] * k;
        frame->out[b] = a->sum_out[b] * k;
        frame->mix[b] = a->sum_mix[b] * k;
    }
    uint32_t seq = a->seq;
    pid_trace_acc_reset(a);
    a->seq = seq;
    return true;
}

static const char *const BOARD_NAME[2] = { "ліве", "праве" };

static void append_side_trace(char *buf, size_t n, size_t *len, const pid_trace_frame_t *f, int b)
{
    switch (f->mode[b]) {
    case PID_SIDE_TWO:
    case PID_SIDE_ONE:
        appendf(buf, n, len, " %s %.1f/%.1f %+.2f", BOARD_NAME[b], (double)f->target_rpm[b], (double)f->speed_rpm[b],
                (double)f->out[b]);
        break;
    case PID_SIDE_OPEN:
        appendf(buf, n, len, " %s без регулятора %.1f мікшер %+.2f", BOARD_NAME[b], (double)f->speed_rpm[b],
                (double)f->mix[b]);
        break;
    case PID_SIDE_NO_ENC:
        appendf(buf, n, len, " %s без енкодерів мікшер %+.2f", BOARD_NAME[b], (double)f->mix[b]);
        break;
    }
}

size_t pid_format_trace(const pid_trace_frame_t *f, char *buf, size_t n)
{
    size_t len = 0;
    if (n) buf[0] = 0;
    appendf(buf, n, &len, "pid %lu мс:", (unsigned long)f->t_ms);
    append_side_trace(buf, n, &len, f, PID_LEFT);
    appendf(buf, n, &len, " |");
    append_side_trace(buf, n, &len, f, PID_RIGHT);
    return len;
}

static const char *mode_text(pid_mode_t m)
{
    switch (m) {
    case PID_MODE_ON: return "увімкнений";
    case PID_MODE_OFF: return "вимкнений";
    case PID_MODE_UNCONFIGURED: break;
    }
    return "не налаштований";
}

size_t pid_format_show(const pid_set_t *s, const pid_side_view_t side[2], pid_rec_result_t load, char *buf, size_t n)
{
    size_t len = 0;
    if (n) buf[0] = 0;
    appendf(buf, n, &len, "регулятор: %s (%s);", mode_text(s->mode), s->saved ? "збережено" : "не збережено");
    if (load == PID_REC_READ) {
        appendf(buf, n, &len, " %s;", pid_record_result_text(load));
    } else if (load != PID_REC_OK && load != PID_REC_ABSENT) {
        appendf(buf, n, &len, " запис відкинуто — %s;", pid_record_result_text(load));
    }
    for (int i = 0; i < PID_PARAM_COUNT; i++) {
        if (s->set[i]) {
            appendf(buf, n, &len, " %s %g", PARAM_NAME[i], (double)s->v[i]);
        } else {
            appendf(buf, n, &len, " %s —", PARAM_NAME[i]);
        }
    }
    if (!pid_set_complete(s)) {
        char miss[32];
        pid_set_missing(s, miss, sizeof miss);
        appendf(buf, n, &len, "; не задані: %s", miss);
    }
    if (side == NULL) return len;
    for (int b = 0; b < 2; b++) {
        const pid_side_view_t *v = &side[b];
        appendf(buf, n, &len, "\n%s: ", BOARD_NAME[b]);
        switch (v->mode) {
        case PID_SIDE_TWO:
            appendf(buf, n, &len, "за двома колесами");
            break;
        case PID_SIDE_ONE:
            appendf(buf, n, &len, "за одним колесом (%s недоступне)", v->missing ? v->missing : "?");
            break;
        case PID_SIDE_OPEN:
            appendf(buf, n, &len, "без регулятора; швидкість %.1f об/хв, мікшер %+.2f", (double)v->speed_rpm,
                    (double)v->mix);
            continue;
        case PID_SIDE_NO_ENC:
            appendf(buf, n, &len, "без енкодерів, без регулятора; мікшер %+.2f", (double)v->mix);
            continue;
        }
        appendf(buf, n, &len, "; задана %.1f об/хв, швидкість %.1f об/хв, вихід %+.2f", (double)v->target_rpm,
                (double)v->speed_rpm, (double)v->out);
    }
    return len;
}

void pid_save_mark(pid_set_t *cur, const pid_set_t *snap, bool ok)
{
    if (!ok) {
        cur->saved = false;   // збій сховища: у сеансі набір діє далі як «не збережено» (spec AC-08, рев'ю N2)
        return;
    }
    // Позначка «збережено» — лише якщо набір не змінився, поки йшов запис.
    if (memcmp(cur->v, snap->v, sizeof(snap->v)) == 0 && cur->mode == snap->mode) cur->saved = true;
}

size_t pid_format_save(const pid_set_t *snap, const char *err, char *buf, size_t n)
{
    if (err != NULL) {
        return (size_t)snprintf(buf, n, "Набір не збережено: збій сховища (%s); набір у цьому сеансі діє далі.", err);
    }
    return (size_t)snprintf(buf, n, "Набір збережено: kp %g, ki %g, kd %g, ramp %g; регулятор %s.",
                            (double)snap->v[PID_PARAM_KP], (double)snap->v[PID_PARAM_KI], (double)snap->v[PID_PARAM_KD],
                            (double)snap->v[PID_PARAM_RAMP], snap->mode == PID_MODE_ON ? "увімкнений" : "вимкнений");
}
