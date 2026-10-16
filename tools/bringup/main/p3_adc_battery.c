// P3 — ADC батареї, двоточкове калібрування. Приймання: VBAT збігається з вольтметром у межах ±50 мВ на 6.0–8.4 В.
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

static const char *TAG = "bringup-p3";

// P3: ADC батареї. GPIO1 = ADC1_CH0, ослаблення 12 dB, curve fitting.
// Друк раз на секунду: сире (усереднене), мВ на піні, VBAT з k=4.033.
// Паралельно вольтметром міряй TP1 і записуй пари (VBAT_друк, вольтметр).
#define VBAT_K_MILLI   4033
#define ADC_SAMPLES    32

void test_p3_adc_battery(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    adc_unit_t unit;
    adc_channel_t chan;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT_SENSE, &unit, &chan));
    ESP_LOGI(TAG, "P3 GPIO%d -> ADC unit %d channel %d", PIN_VBAT_SENSE, (int)unit, (int)chan);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, chan, &chan_cfg));

    adc_cali_handle_t cali;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = chan,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali));

    for (;;) {
        int sum = 0;
        for (int i = 0; i < ADC_SAMPLES; i++) {
            int raw;
            ESP_ERROR_CHECK(adc_oneshot_read(adc, chan, &raw));
            sum += raw;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        int raw_avg = sum / ADC_SAMPLES;
        int mv = 0;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali, raw_avg, &mv));
        int vbat_mv = mv * VBAT_K_MILLI / 1000;
        ESP_LOGI(TAG, "P3 raw=%d  adc=%d mV  VBAT=%d mV (k=%d.%03d)",
                 raw_avg, mv, vbat_mv, VBAT_K_MILLI / 1000, VBAT_K_MILLI % 1000);
        vTaskDelay(pdMS_TO_TICKS(840));
    }
}
