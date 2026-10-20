// HAL регулятора швидкості (phase5-pid; ADR-0002, ADR-0003): набір під portMUX, запис у NVS, задача pid_trace.
#include "pid_hal.h"

#include <stdio.h>
#include <string.h>

#include "app_state_hal.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define PID_NVS_NAMESPACE "pid"
#define PID_NVS_KEY "set"
#define TRACE_PERIOD_MS 100   // 10 рядків за секунду (spec AC-05)
#define TRACE_STACK 3072
#define TRACE_PRIORITY 1
#define TRACE_CORE 0

static const char *TAG = "pid";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static pid_set_t s_set;                 // поточний набір (змінює консоль, копіює control)
static pid_guard_t s_guard;             // запис набору, зміна режиму й вхід в ARMED взаємно виключні (spec AC-15)
static pid_side_view_t s_view[2];       // останній стан бортів від control
static pid_trace_frame_t s_frame;       // останній кадр трасування від control
static bool s_trace_on;
static pid_rec_result_t s_load = PID_REC_ABSENT;   // результат завантаження запису на старті (spec AC-07)

void pid_hal_init(void)
{
    pid_set_t loaded;
    pid_set_init(&loaded);
    uint8_t buf[PID_RECORD_SIZE + 8];
    size_t len = sizeof(buf);
    pid_rec_result_t r;
    nvs_handle_t h;
    esp_err_t err = nvs_open(PID_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        r = PID_REC_ABSENT;   // простору ще немає: набір ніколи не зберігали
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "регулятор не налаштований: %s (%s)", pid_record_result_text(PID_REC_READ), esp_err_to_name(err));
        r = PID_REC_READ;
    } else {
        err = nvs_get_blob(h, PID_NVS_KEY, buf, &len);
        nvs_close(h);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            r = PID_REC_ABSENT;
        } else if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            r = PID_REC_SIZE;
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "регулятор не налаштований: %s (%s)", pid_record_result_text(PID_REC_READ), esp_err_to_name(err));
            r = PID_REC_READ;
        } else {
            r = pid_record_decode(buf, len, &loaded);
        }
    }
    portENTER_CRITICAL(&s_mux);
    s_set = loaded;
    s_load = r;
    portEXIT_CRITICAL(&s_mux);
    if (r == PID_REC_OK) {
        ESP_LOGI(TAG, "набір завантажено: kp %g, ki %g, kd %g, ramp %g; регулятор %s", (double)loaded.v[PID_PARAM_KP],
                 (double)loaded.v[PID_PARAM_KI], (double)loaded.v[PID_PARAM_KD], (double)loaded.v[PID_PARAM_RAMP],
                 loaded.mode == PID_MODE_ON ? "увімкнений" : "вимкнений");
    } else if (r == PID_REC_ABSENT) {
        ESP_LOGI(TAG, "регулятор не налаштований: %s", pid_record_result_text(r));
    } else if (r != PID_REC_READ) {   // збій читання вже записано одним рядком вище (рев'ю F9)
        ESP_LOGW(TAG, "регулятор не налаштований: запис відкинуто — %s", pid_record_result_text(r));
    }
}

void pid_hal_get(pid_set_t *out, bool *saving)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_set;
    *saving = s_guard.saving;
    portEXIT_CRITICAL(&s_mux);
}

size_t pid_hal_set_param(pid_param_t p, float value, char *buf, size_t n)
{
    portENTER_CRITICAL(&s_mux);
    bool was_set = s_set.set[p];
    float old = s_set.v[p];
    const char *why = pid_set_param(&s_set, p, value);
    pid_set_t now = s_set;
    portEXIT_CRITICAL(&s_mux);
    if (why) return (size_t)snprintf(buf, n, "Команда pid: %s — %s.", pid_param_name(p), why);
    char miss[32];
    pid_set_missing(&now, miss, sizeof(miss));
    char prev[24];
    if (was_set) {
        snprintf(prev, sizeof(prev), "%g", (double)old);
    } else {
        snprintf(prev, sizeof(prev), "—");
    }
    return (size_t)snprintf(buf, n, "%s: %s → %g; набір не збережено%s%s.", pid_param_name(p), prev, (double)value,
                            miss[0] ? "; ще не задані: " : "", miss);
}

size_t pid_hal_set_mode(bool on, char *buf, size_t n)
{
    // Зміна режиму тримає власну ознаку (рев'ю N4, R1; шаблон F1): поки вона стоїть, control не візьме дозвіл на
    // ARMED, тож стан, прочитаний нижче, не зміниться до запису режиму.
    portENTER_CRITICAL(&s_mux);
    bool ok = pid_guard_try_mode(&s_guard);
    portEXIT_CRITICAL(&s_mux);
    if (!ok) {
        return (size_t)snprintf(buf, n, "Регулятор не перемкнено: триває озброєння чи збереження набору; повторіть у DISARMED.");
    }
    bool disarmed = app_state_get() == APP_STATE_DISARMED;
    portENTER_CRITICAL(&s_mux);
    const char *why = pid_mode_change(&s_set, on, disarmed);
    pid_set_t now = s_set;
    pid_guard_end_mode(&s_guard);
    portEXIT_CRITICAL(&s_mux);
    if (why && !disarmed) return (size_t)snprintf(buf, n, "Регулятор не перемкнено: %s.", why);
    if (why) {
        char miss[32];
        pid_set_missing(&now, miss, sizeof(miss));
        return (size_t)snprintf(buf, n, "Регулятор не ввімкнено: %s (не задані: %s).", why, miss);
    }
    return (size_t)snprintf(buf, n, "Регулятор %s; діє з наступного озброєння; набір не збережено.",
                            on ? "увімкнено" : "вимкнено");
}

size_t pid_hal_save(char *buf, size_t n)
{
    // Ознака — до перевірки стану й атомарно з перевіркою «озброєння не триває» (spec AC-15, рев'ю F1): control
    // бере дозвіл на ARMED під тим самим portMUX, тож запис і вхід в ARMED не перетинаються.
    portENTER_CRITICAL(&s_mux);
    bool ok = pid_guard_try_save(&s_guard);
    pid_set_t snap = s_set;
    portEXIT_CRITICAL(&s_mux);
    if (!ok) {
        return (size_t)snprintf(buf, n, "Набір не збережено: триває озброєння чи інше збереження; повторіть у DISARMED.");
    }

    size_t len;
    app_state_t st = app_state_get();
    uint8_t rec[PID_RECORD_SIZE];
    if (st == APP_STATE_ARMED || st == APP_STATE_OBSTACLE_HOLD) {
        len = (size_t)snprintf(buf, n, "Набір не збережено: запис у памʼять плати під час руху заборонений (стан %s).",
                               app_state_name(st));
    } else if (pid_record_encode(&snap, rec) == 0) {
        char miss[32];
        pid_set_missing(&snap, miss, sizeof(miss));
        len = (size_t)snprintf(buf, n, "Набір не збережено: набір неповний (не задані: %s).", miss);
    } else {
        nvs_handle_t h;
        esp_err_t err = nvs_open(PID_NVS_NAMESPACE, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_blob(h, PID_NVS_KEY, rec, sizeof(rec));
            if (err == ESP_OK) err = nvs_commit(h);
            nvs_close(h);
        }
        // Рішення й текст — у pid_core (spec AC-08); під s_mux лише позначка, текст — поза секцією (рев'ю N3).
        portENTER_CRITICAL(&s_mux);
        pid_save_mark(&s_set, &snap, err == ESP_OK);
        if (err == ESP_OK) s_load = PID_REC_OK;   // відкинутий запис замінено новим
        portEXIT_CRITICAL(&s_mux);
        len = pid_format_save(&snap, err == ESP_OK ? NULL : esp_err_to_name(err), buf, n);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "%s", buf);
        } else {
            ESP_LOGW(TAG, "%s", buf);
        }
    }

    portENTER_CRITICAL(&s_mux);
    pid_guard_end_save(&s_guard);
    portEXIT_CRITICAL(&s_mux);
    return len;
}

size_t pid_hal_show(char *buf, size_t n)
{
    pid_set_t set;
    pid_side_view_t view[2];
    pid_rec_result_t load;
    portENTER_CRITICAL(&s_mux);
    set = s_set;
    load = s_load;
    view[0] = s_view[0];
    view[1] = s_view[1];
    portEXIT_CRITICAL(&s_mux);
    return pid_format_show(&set, view, load, buf, n);
}

bool pid_hal_arm_begin(pid_busy_t *why)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = pid_guard_try_arm(&s_guard, why);
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void pid_hal_arm_end(void)
{
    portENTER_CRITICAL(&s_mux);
    pid_guard_end_arm(&s_guard);
    portEXIT_CRITICAL(&s_mux);
}

void pid_hal_publish_view(const pid_side_view_t view[2])
{
    portENTER_CRITICAL(&s_mux);
    s_view[0] = view[0];
    s_view[1] = view[1];
    portEXIT_CRITICAL(&s_mux);
}

void pid_hal_publish_trace(const pid_trace_frame_t *frame)
{
    portENTER_CRITICAL(&s_mux);
    s_frame = *frame;
    portEXIT_CRITICAL(&s_mux);
}

void pid_hal_trace_enable(bool on)
{
    portENTER_CRITICAL(&s_mux);
    s_trace_on = on;
    portEXIT_CRITICAL(&s_mux);
}

// Друкує новий кадр трасування, якщо трасування ввімкнене і кадр із ARMED (ADR-0003: повільний USB блокує лише цю задачу).
static void trace_task(void *arg)
{
    (void)arg;
    esp_err_t wdt = esp_task_wdt_add(NULL);
    if (wdt != ESP_OK) {
        ESP_LOGE(TAG, "esp_task_wdt_add: %s — pid_trace працює без сторожового таймера", esp_err_to_name(wdt));
    }
    static char line[200];
    uint32_t printed_seq = 0;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TRACE_PERIOD_MS));
        if (wdt == ESP_OK) esp_task_wdt_reset();
        pid_trace_frame_t f;
        bool on;
        portENTER_CRITICAL(&s_mux);
        f = s_frame;
        on = s_trace_on;
        portEXIT_CRITICAL(&s_mux);
        if (!on || !f.armed || f.seq == printed_seq) continue;
        printed_seq = f.seq;
        pid_format_trace(&f, line, sizeof(line));
        printf("%s\r\n", line);
    }
}

esp_err_t pid_trace_start(void)
{
    if (xTaskCreatePinnedToCore(trace_task, "pid_trace", TRACE_STACK, NULL, TRACE_PRIORITY, NULL, TRACE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу pid_trace — трасування недоступне");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
