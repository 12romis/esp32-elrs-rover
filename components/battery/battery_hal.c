// HAL вимірювання напруги: adc_oneshot + curve fitting, пакет із 16 відліків за такт, знімок для control.
// Логіка (перерахунок, поріг, свіжість) — у battery_core.c.
#include "battery_hal.h"

#include "battery_core.h"
#include "board_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATTERY_TASK_STACK 3072
#define BATTERY_TASK_PRIORITY 2
#define BATTERY_TASK_CORE 0
#define BATTERY_PERIOD_MS 200
#define BATTERY_BURST 16  // відліків за такт (документ 02, §3.1: 16–64)

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static battery_snapshot_t s_snap;  // valid=false до першого вимірювання

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void battery_get_snapshot(battery_snapshot_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_snap;
    portEXIT_CRITICAL(&s_lock);
}

static bool measure(uint32_t *vbat_mv)
{
    uint16_t samples[BATTERY_BURST];
    for (int i = 0; i < BATTERY_BURST; i++) {
        int raw = 0;
        int mv = 0;
        if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK ||
            adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK || mv < 0) {
            return false;
        }
        samples[i] = (uint16_t)mv;
        vTaskDelay(1);
    }
    *vbat_mv = battery_adc_to_mv(battery_avg_mv(samples, BATTERY_BURST));
    return true;
}

static void battery_task(void *arg)
{
    (void)arg;
    bool first = true;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t mv;
        if (measure(&mv)) {
            battery_snapshot_t s = {.mv = mv, .t_ms = now_ms(), .valid = true};
            portENTER_CRITICAL(&s_lock);
            s_snap = s;
            portEXIT_CRITICAL(&s_lock);
            if (first) {
                first = false;
                if (battery_is_below_cutoff(mv)) {
                    ESP_LOGW(TAG, "напруга %u мВ нижча за поріг (озброєння буде відхилено) або батареї немає", (unsigned)mv);
                } else {
                    ESP_LOGI(TAG, "напруга %u мВ", (unsigned)mv);
                }
            }
        } else {
            ESP_LOGW(TAG, "збій вимірювання напруги — знімок не оновлено");
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(BATTERY_PERIOD_MS));
    }
}

esp_err_t battery_start(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(err));
        return err;
    }
    adc_unit_t unit;
    err = adc_oneshot_io_to_channel(PIN_VBAT_SENSE, &unit, &s_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_io_to_channel: %s", esp_err_to_name(err));
        return err;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {.bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12};
    err = adc_oneshot_config_channel(s_adc, s_chan, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(err));
        return err;
    }
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_chan,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_cali_create_scheme_curve_fitting: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreatePinnedToCore(battery_task, "battery", BATTERY_TASK_STACK, NULL, BATTERY_TASK_PRIORITY, NULL,
                                BATTERY_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу battery");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
