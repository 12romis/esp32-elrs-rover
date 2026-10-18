// P8 — вбудований RGB-індикатор модуля (spec phase2-io, OQ-3). Результат записується у docs/measured.md.
// Гіпотеза з фото: поруч із світлодіодом підписана перемичка IN-OUT, тобто схоже на адресований світлодіод з одним дротом даних.
// Тест нічого не вважає підтвердженим: шле три контрольні трійки байтів і друкує, що саме подано, а Розробник записує, що побачив.
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

static const char *TAG = "bringup-p8";

#define RMT_RES_HZ 10000000   // 0.1 мкс на такт
#define LEVEL 40              // помірна яскравість, щоб не засліплювати

static void send_rgb(rmt_channel_handle_t ch, rmt_encoder_handle_t enc, uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t buf[3] = {a, b, c};
    rmt_transmit_config_t tx = {.loop_count = 0};
    ESP_ERROR_CHECK(rmt_transmit(ch, enc, buf, sizeof(buf), &tx));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(ch, 100));
    vTaskDelay(pdMS_TO_TICKS(1));  // пауза скидання ≥ 50 мкс
}

void test_p8_indicator(void)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num = PIN_STATUS_RGB,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RES_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    rmt_channel_handle_t ch;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &ch));
    rmt_bytes_encoder_config_t ecfg = {
        .bit0 = {.level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8},   // 0.4 мкс висока, 0.8 низька
        .bit1 = {.level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 5},   // 0.8 мкс висока, 0.5 низька
        .flags.msb_first = 1,
    };
    rmt_encoder_handle_t enc;
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&ecfg, &enc));
    ESP_ERROR_CHECK(rmt_enable(ch));
    ESP_LOGI(TAG, "P8 старт: лінія з board_pins.h, 10 МГц, байти шлються як є (порядок кольорів невідомий)");

    for (;;) {
        ESP_LOGI(TAG, "P8 байти (%d,0,0) — який колір видно (або нічого)?", LEVEL);
        send_rgb(ch, enc, LEVEL, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "P8 байти (0,%d,0) — який колір видно?", LEVEL);
        send_rgb(ch, enc, 0, LEVEL, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "P8 байти (0,0,%d) — який колір видно?", LEVEL);
        send_rgb(ch, enc, 0, 0, LEVEL);
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "P8 байти (0,0,0) — має погаснути");
        send_rgb(ch, enc, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
