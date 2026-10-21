// HAL telemetry (ADR-0001, ADR-0003): задача 200 мс, запис кадру через crsf_hal_send, статистика кроку під portMUX.
#include "telemetry_hal.h"

#include "battery_hal.h"
#include "crsf_hal.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TELEMETRY_STACK 4096
#define TELEMETRY_PRIORITY 2
#define TELEMETRY_CORE 0

static const char *TAG = "telemetry";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static tel_step_stats_t s_step;
static uint32_t s_period_us;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void telemetry_step_record(uint32_t us, uint32_t period_us)
{
    portENTER_CRITICAL(&s_lock);
    s_period_us = period_us;
    telemetry_core_step_record(&s_step, us, period_us);
    portEXIT_CRITICAL(&s_lock);
}

void telemetry_step_get(tel_step_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_step;
    portEXIT_CRITICAL(&s_lock);
}

size_t telemetry_step_text(char *buf, size_t n)
{
    tel_step_stats_t s;
    portENTER_CRITICAL(&s_lock);
    s = s_step;
    uint32_t period = s_period_us;
    portEXIT_CRITICAL(&s_lock);
    return telemetry_core_step_text(&s, period, buf, n);
}

static void telemetry_task(void *arg)
{
    (void)arg;
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        // telemetry не критична для безпеки: працює й без Task WDT, але це видно в журналі.
        ESP_LOGE(TAG, "esp_task_wdt_add: %s — telemetry працює без сторожового таймера", esp_err_to_name(err));
    }
    tel_link_t link;
    telemetry_core_link_init(&link);
    tel_link_t snap_link;    // «знімок придатний ↔ ні»: одне попередження на зміну (логіка в core)
    telemetry_core_link_init(&snap_link);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        battery_snapshot_t snap;
        battery_get_snapshot(&snap);
        uint32_t now = now_ms();
        tel_event_t sev;
        bool send = telemetry_core_decide(&snap_link, &snap, now, &sev);
        if (sev == TEL_EVT_FAILED) {
            ESP_LOGW(TAG, "немає придатного знімка напруги — кадр телеметрії не надсилається");
        } else if (sev == TEL_EVT_RESTORED) {
            ESP_LOGW(TAG, "знімок напруги знову придатний — телеметрію відновлено");
        }
        if (send) {
            uint8_t frame[TELEMETRY_BATTERY_FRAME_LEN];
            size_t len = telemetry_core_build_battery_frame(snap.mv, frame, sizeof(frame));
            tel_event_t ev = telemetry_core_link_report(&link, len != 0 && crsf_hal_send(frame, len));
            if (ev == TEL_EVT_FAILED) {
                ESP_LOGW(TAG, "кадр телеметрії не передано (приймач недоступний чи збій запису) — ровер працює як зазвичай");
            } else if (ev == TEL_EVT_RESTORED) {
                ESP_LOGI(TAG, "передачу телеметрії відновлено");
            }
        }
        if (err == ESP_OK) {
            esp_task_wdt_reset();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

esp_err_t telemetry_start(void)
{
    // s_step — статична, вже нульова; обнуляти її тут не можна: control могла почати писати (гонка на двох ядрах)
    if (xTaskCreatePinnedToCore(telemetry_task, "telemetry", TELEMETRY_STACK, NULL, TELEMETRY_PRIORITY, NULL, TELEMETRY_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу telemetry — телеметрії не буде");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
