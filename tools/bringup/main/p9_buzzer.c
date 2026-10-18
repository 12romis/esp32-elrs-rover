// P9 — зумер: які частоти гучні (spec phase2-io, OQ-2). Результат записується у docs/measured.md.
// Зумер пасивний (за словами власника — «12085, 42 Ом»). Тест по черзі подає частоти з діапазону 200–5000 Гц і друкує поточну.
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

static const char *TAG = "bringup-p9";

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_BUZZER LEDC_TIMER_1
#define LEDC_CH_BUZZER LEDC_CHANNEL_2
#define DUTY_HALF 512   // 50 % шкали 10 біт

void test_p9_buzzer(void)
{
    static const uint32_t freqs[] = {200, 300, 400, 500, 700, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000};
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER_BUZZER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = freqs[0],
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_BUZZER,
        .timer_sel = LEDC_TIMER_BUZZER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ESP_ERROR_CHECK(ledc_stop(LEDC_MODE, LEDC_CH_BUZZER, 0));
    ESP_LOGI(TAG, "P9 старт: зумер у спокої (низький рівень). Запишіть, на яких частотах чути найгучніше");

    for (;;) {
        for (size_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
            ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, LEDC_TIMER_BUZZER, freqs[i]));
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CH_BUZZER, DUTY_HALF));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CH_BUZZER));
            ESP_LOGI(TAG, "P9 %u Гц", (unsigned)freqs[i]);
            vTaskDelay(pdMS_TO_TICKS(700));
            ESP_ERROR_CHECK(ledc_stop(LEDC_MODE, LEDC_CH_BUZZER, 0));
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        ESP_LOGI(TAG, "P9 коло завершено, повтор через 3 с");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
