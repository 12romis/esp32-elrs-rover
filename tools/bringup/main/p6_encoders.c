// P6 — енкодери, обертання рукою. Вимагає прозвонених моторних шлейфів (розділ 4 док. 01).
// Процедура: колесо в повітрі, повернути рукою рівно 10 обертів, записати приріст, поділити на 10.
// Тричі й середнє. Два прогони: P6_PULLUP 0 і 1 (з внутрішніми підтяжками).
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

#define P6_PULLUP 0   // 0 = без підтяжок, 1 = gpio_pullup_en() на всіх восьми лініях

static const char *TAG = "bringup-p6";

const char *const ENC_NAMES[4] = { "FL", "FR", "RL", "RR" };

static const int ENC_PINS[4][2] = {
    { PIN_ENC_FL_A, PIN_ENC_FL_B },
    { PIN_ENC_FR_A, PIN_ENC_FR_B },
    { PIN_ENC_RL_A, PIN_ENC_RL_B },
    { PIN_ENC_RR_A, PIN_ENC_RR_B },
};

static pcnt_unit_handle_t s_unit[4];

void enc_init(int use_pullup)
{
    for (int i = 0; i < 4; i++) {
        pcnt_unit_config_t unit_cfg = {
            .high_limit = 30000,
            .low_limit = -30000,
            .flags.accum_count = true,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_unit[i]));

        // Енкодери в одному шлейфі з моторними дротами: glitch-фільтр обовʼязковий (§4.8)
        pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 1000 };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_unit[i], &filter));

        pcnt_channel_handle_t chan_a, chan_b;
        pcnt_chan_config_t cfg_a = {
            .edge_gpio_num = ENC_PINS[i][0],
            .level_gpio_num = ENC_PINS[i][1],
        };
        ESP_ERROR_CHECK(pcnt_new_channel(s_unit[i], &cfg_a, &chan_a));
        pcnt_chan_config_t cfg_b = {
            .edge_gpio_num = ENC_PINS[i][1],
            .level_gpio_num = ENC_PINS[i][0],
        };
        ESP_ERROR_CHECK(pcnt_new_channel(s_unit[i], &cfg_b, &chan_b));

        // ×4 роздільність: обидва канали по обох фронтах
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

        // Акумуляція при досягненні меж: потрібні watch point на обох межах
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_unit[i], 30000));
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(s_unit[i], -30000));

        if (use_pullup) {
            ESP_ERROR_CHECK(gpio_pullup_en(ENC_PINS[i][0]));
            ESP_ERROR_CHECK(gpio_pullup_en(ENC_PINS[i][1]));
        }

        ESP_ERROR_CHECK(pcnt_unit_enable(s_unit[i]));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(s_unit[i]));
        ESP_ERROR_CHECK(pcnt_unit_start(s_unit[i]));
    }
}

int enc_count(int idx)
{
    int v = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(s_unit[idx], &v));
    return v;
}

void test_p6_encoders(void)
{
    enc_init(P6_PULLUP);
    ESP_LOGI(TAG, "P6 pullup=%d. Крути колесо рукою, рахуй 10 обертів між двома рядками", P6_PULLUP);

    int prev[4] = { 0 };
    for (;;) {
        int cur[4];
        for (int i = 0; i < 4; i++) {
            cur[i] = enc_count(i);
        }
        ESP_LOGI(TAG, "P6 FL=%d(%+d) FR=%d(%+d) RL=%d(%+d) RR=%d(%+d)",
                 cur[0], cur[0] - prev[0], cur[1], cur[1] - prev[1],
                 cur[2], cur[2] - prev[2], cur[3], cur[3] - prev[3]);
        for (int i = 0; i < 4; i++) {
            prev[i] = cur[i];
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
