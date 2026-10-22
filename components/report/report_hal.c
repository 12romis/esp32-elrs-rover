// HAL report (phase8-session-report, ADR-0003, ADR-0004): див. report_hal.h.
#include "report_hal.h"

#include <stdio.h>
#include <string.h>

#include "app_state_hal.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "report_core.h"
#include "report_net_hal.h"
#include "report_store_hal.h"
#include "session_hal.h"

static const char *TAG = "report";

#define REPORT_STACK 8192          // TLS (mbedtls) і NVS потребують запасу
#define REPORT_PRIORITY 1          // нижче за всі періодичні задачі
#define REPORT_CORE 0              // разом зі стеком Wi-Fi (документ 02 §6.1)
#define POLL_MS 100                // опитування стану не рідше ніж раз на 100 мс (ADR-0003)

static SemaphoreHandle_t s_lock;
static report_q_t s_q;
static volatile bool s_busy;
static volatile bool s_request;            // команда `report` просить спробу негайно
static uint32_t s_request_ms;
static report_outcome_t s_last_outcome = REPORT_OUT_NONE;
static uint32_t s_last_outcome_ms;
static report_fw_info_t s_fw;
static bool s_started;
static uint32_t s_attempt_start_ms;
static bool s_timed_out;
static uint32_t s_pending_remove[REPORT_Q_SLOTS];   // доставлені звіти, чиї слоти ще не видалено з NVS
static uint8_t s_pending_n;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void fw_info_init(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    report_fw_from_version(&s_fw, d != NULL ? d->version : NULL);
    if (d != NULL) {
        strlcpy(s_fw.idf, d->idf_ver, sizeof(s_fw.idf));
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_fw.mac, sizeof(s_fw.mac), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void report_hal_status(report_status_t *s)
{
    memset(s, 0, sizeof(*s));
    if (!s_started) {
        s->config_ok = report_net_config_ok();
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s->queue = s_q;
    s->last_outcome = s_last_outcome;
    s->last_outcome_ms = s_last_outcome_ms;
    xSemaphoreGive(s_lock);
    s->busy = s_busy;
    s->config_ok = report_net_config_ok();
    s->commits = report_store_commits();
}

const char *report_hal_config_problem(void)
{
    return report_net_config_problem();
}

report_gate_t report_hal_attempt_now(void)
{
    report_status_t st;
    report_hal_status(&st);
    report_gate_t g = report_attempt_gate(app_state_get(), st.busy || s_request, report_q_count(&st.queue), st.config_ok);
    if (g == REPORT_GO && s_started) {
        s_request_ms = now_ms();
        s_request = true;
    }
    return g;
}

// Новий підсумок сесії → документ → NVS (один коміт) → черга. Викликається лише коли мотори вимкнено.
static void store_summary(const session_summary_t *sum)
{
    char json[REPORT_JSON_MAX + 1];
    size_t n = report_core_build_json(json, sizeof(json), sum, &s_fw);
    ESP_LOGI(TAG, "сесія %lu завершена: %s", (unsigned long)sum->seq, report_end_reason_text(sum->end_reason));
    if (n == 0) {
        ESP_LOGE(TAG, "документ звіту сесії %lu не сформовано — звіт втрачено", (unsigned long)sum->seq);
        return;
    }
    esp_err_t err = report_store_put(sum->seq, sum->end_reason, json, n, session_hal_next_seq());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "звіт сесії %lu не збережено в памʼять плати (%s) — втрачено", (unsigned long)sum->seq, esp_err_to_name(err));
        return;
    }
    uint32_t evicted = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ev = report_q_push(&s_q, sum->seq, sum->end_reason, &evicted);
    xSemaphoreGive(s_lock);
    if (ev) {
        ESP_LOGW(TAG, "видалено найстарший звіт, сесія %lu", (unsigned long)evicted);
    }
}

static bool attempt_allow(void)
{
    if (!report_allow_send(app_state_get())) {
        return false;
    }
    if ((uint32_t)(now_ms() - s_attempt_start_ms) >= REPORT_ATTEMPT_LIMIT_MS) {
        s_timed_out = true;
        return false;
    }
    return true;
}

// Одна спроба передачі (ADR-0003): Wi-Fi вгору → TLS+MQTT → звіти від найстаршого → закрити → Wi-Fi вниз. Слоти доставлених
// звітів видаляються вже після вимкнення Wi-Fi (у головному циклі, коли мотори вимкнено).
static void run_attempt(uint32_t start_ms)
{
    s_busy = true;
    s_attempt_start_ms = start_ms;
    s_timed_out = false;
    report_net_t ctx = {.allow = attempt_allow, .start_ms = start_ms};
    strlcpy(ctx.mac, s_fw.mac, sizeof(ctx.mac));
    uint32_t heap_before = report_net_free_heap();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    report_q_t snap = s_q;
    xSemaphoreGive(s_lock);
    report_attempt_t a;
    report_attempt_begin(&a, &snap);
    ESP_LOGI(TAG, "спроба передачі: звітів у черзі %u, Wi-Fi вмикається", (unsigned)report_q_count(&snap));

    report_outcome_t fail = report_net_wifi_up(&ctx);
    if (fail == REPORT_OUT_NONE) {
        fail = report_net_mqtt_open(&ctx);
    }
    if (fail != REPORT_OUT_NONE) {
        a.stopped = true;      // зв'язку немає: жоден звіт не передавався
        a.outcome = fail;
    } else {
        uint32_t seq;
        while (report_attempt_next(&a, &seq)) {
            static char json[REPORT_JSON_MAX + 1];
            size_t len = 0;
            report_outcome_t o;
            if (report_store_load(seq, json, sizeof(json), &len) != ESP_OK) {
                ESP_LOGW(TAG, "звіт сесії %lu не прочитано з памʼяті плати", (unsigned long)seq);
                o = REPORT_OUT_NO_ACK;
            } else {
                o = report_net_mqtt_publish(&ctx, json, len, (uint16_t)(seq % 65535u + 1u));
            }
            if (o == REPORT_OUT_DELIVERED && s_pending_n < REPORT_Q_SLOTS) {
                s_pending_remove[s_pending_n++] = seq;
            }
            report_attempt_result(&a, o);
        }
        report_net_mqtt_close();
    }
    report_net_wifi_down();
    report_outcome_t result = report_attempt_outcome(&a);
    if (s_timed_out && result == REPORT_OUT_ABORTED) {
        result = REPORT_OUT_TIMEOUT;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < s_pending_n; i++) {
        report_q_mark_sent(&s_q, s_pending_remove[i]);
    }
    s_last_outcome = result;
    s_last_outcome_ms = now_ms();
    xSemaphoreGive(s_lock);
    uint32_t heap_after = report_net_free_heap();
    ESP_LOGI(TAG, "спроба передачі завершена за %lu мс: %s; доставлено %u; Wi-Fi вимкнено; вільна памʼять %lu → %lu байт",
             (unsigned long)(now_ms() - start_ms), report_outcome_text(result), (unsigned)a.delivered,
             (unsigned long)heap_before, (unsigned long)heap_after);
    s_busy = false;
}

// Видалення слотів доставлених звітів з NVS: лише коли мотори вимкнено (запис у флеш).
static void flush_pending_remove(void)
{
    for (uint8_t i = 0; i < s_pending_n; i++) {
        esp_err_t err = report_store_remove(s_pending_remove[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "слот звіту сесії %lu не видалено (%s): повторна передача безпечна, сервер відсіче дубль",
                     (unsigned long)s_pending_remove[i], esp_err_to_name(err));
        }
    }
    s_pending_n = 0;
}

static void report_task(void *arg)
{
    (void)arg;
    app_state_t prev = APP_STATE_BOOT;
    bool entry_pending = false;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        app_state_t st = app_state_get();
        bool motors_off = !app_state_is_driving(st);
        if (motors_off) {
            // Записи у флеш (NVS) лише за вимкнених моторів: підсумки сесій і видалення доставлених звітів.
            session_summary_t sum;
            while (session_hal_receive(&sum, 0)) {
                store_summary(&sum);
            }
            if (s_pending_n > 0) {
                flush_pending_remove();
            }
        }
        // Вхід у DISARMED запам'ятовується до закриття сесії: підсумок потрапляє в чергу на кроці після переходу (рев'ю, знак. 1).
        entry_pending = report_entry_pending(entry_pending, prev, st);
        prev = st;
        bool entered_disarmed = report_entry_ready(entry_pending, st, session_hal_active());
        if (entered_disarmed) {
            // Підсумки, що встигли надійти між зчитуванням вище й закриттям сесії, зберігаємо до рішення про спробу.
            session_summary_t late;
            while (session_hal_receive(&late, 0)) {
                store_summary(&late);
            }
            entry_pending = false;
        }
        bool from_cmd = s_request;
        if (entered_disarmed || from_cmd) {
            s_request = false;
            report_status_t status;
            report_hal_status(&status);
            report_gate_t g = report_attempt_gate(st, false, report_q_count(&status.queue), status.config_ok);
            if (g == REPORT_GO) {
                run_attempt(from_cmd ? s_request_ms : now_ms());
            } else if (g == REPORT_DENY_NO_CONFIG && entered_disarmed) {
                ESP_LOGW(TAG, "налаштування мережі не задано, передача не починається (%s)", report_net_config_problem());
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t report_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    fw_info_init();
    uint32_t next_seq = 1;
    esp_err_t err = report_store_init(&s_q, &next_seq);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "сховище звітів недоступне (%s) — звіти сесій не зберігаються", esp_err_to_name(err));
        return err;
    }
    session_hal_raise_next_seq(next_seq);
    report_net_init();
    if (xTaskCreatePinnedToCore(report_task, "report", REPORT_STACK, NULL, REPORT_PRIORITY, NULL, REPORT_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу report");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "звіти сесій: у черзі %u, наступна сесія %lu, налаштування мережі %s", (unsigned)report_q_count(&s_q),
             (unsigned long)next_seq, report_net_config_ok() ? "задано" : "не задано");
    return ESP_OK;
}
