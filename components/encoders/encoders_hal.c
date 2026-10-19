// HAL енкодерів коліс (ADR-0001, ADR-0002): чотири блоки лічильника імпульсів, задача encoders, стан під portMUX.
// Логіка накопичення, швидкості й текстів — в encoders_core.c.
#include "encoders_hal.h"

#include "board_pins.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "encoders";

#define ENC_PERIOD_MS (ENC_PERIOD_US / 1000)   // період задачі — номінал вибірки з encoders_core.h
#define ENC_STACK 3072
#define ENC_PRIORITY 4
#define ENC_CORE 1
#define ENC_GLITCH_NS 1000          // фільтр завад від моторних дротів у тому самому шлейфі (документ 02 §4.8)
#define ENC_GAP_LOG_US 750000ULL    // 15000 імпульсів на верхній межі spec §6 (20 000 імп/с): пропуск варто бачити

// Лінії A і B кожного колеса в порядку FL, FR, RL, RR (відповідність коліс лічильникам — P6, docs/measured.md).
static const int s_pins[ENC_WHEELS][2] = {
    { PIN_ENC_FL_A, PIN_ENC_FL_B },
    { PIN_ENC_FR_A, PIN_ENC_FR_B },
    { PIN_ENC_RL_A, PIN_ENC_RL_B },
    { PIN_ENC_RR_A, PIN_ENC_RR_B },
};

static const char *const s_names[ENC_WHEELS] = { "FL", "FR", "RL", "RR" };
static pcnt_unit_handle_t s_unit[ENC_WHEELS];
static enc_state_t s_state;                     // під s_mux: пише задача encoders, обнуляє консоль
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// Налаштування одного колеса як у P6 (×4 через два канали), але без accum_count: watch-point на межах без callback
// лише вмикають апаратні порівнювачі меж, тож лічильник скидається в нуль на ±ENC_HW_LIMIT (ADR-0002). Підтяжки не
// вмикаються (ENC_NEEDS_PULLUP = 0, P6). Повертає першу помилку; частково налаштований блок лишається як є (лише на старті).
static esp_err_t setup_wheel(int i)
{
    const pcnt_unit_config_t unit_cfg = {
        .high_limit = ENC_HW_LIMIT,
        .low_limit = -ENC_HW_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&unit_cfg, &s_unit[i]);
    if (err != ESP_OK) return err;

    const pcnt_glitch_filter_config_t filter = { .max_glitch_ns = ENC_GLITCH_NS };
    if ((err = pcnt_unit_set_glitch_filter(s_unit[i], &filter)) != ESP_OK) return err;

    pcnt_channel_handle_t chan_a, chan_b;
    const pcnt_chan_config_t cfg_a = { .edge_gpio_num = s_pins[i][0], .level_gpio_num = s_pins[i][1] };
    if ((err = pcnt_new_channel(s_unit[i], &cfg_a, &chan_a)) != ESP_OK) return err;
    const pcnt_chan_config_t cfg_b = { .edge_gpio_num = s_pins[i][1], .level_gpio_num = s_pins[i][0] };
    if ((err = pcnt_new_channel(s_unit[i], &cfg_b, &chan_b)) != ESP_OK) return err;

    // Ті самі дії фронтів і рівнів, що в P6: на них виміряно знаки коліс (ENC_DIR_SIGN).
    if ((err = pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_INCREASE)) != ESP_OK) return err;
    if ((err = pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                             PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK) return err;
    if ((err = pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_DECREASE)) != ESP_OK) return err;
    if ((err = pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                             PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK) return err;

    if ((err = pcnt_unit_add_watch_point(s_unit[i], ENC_HW_LIMIT)) != ESP_OK) return err;
    if ((err = pcnt_unit_add_watch_point(s_unit[i], -ENC_HW_LIMIT)) != ESP_OK) return err;

    if ((err = pcnt_unit_enable(s_unit[i])) != ESP_OK) return err;
    if ((err = pcnt_unit_clear_count(s_unit[i])) != ESP_OK) return err;
    return pcnt_unit_start(s_unit[i]);
}

static void encoders_task(void *arg)
{
    (void)arg;
    esp_err_t wdt = esp_task_wdt_add(NULL);
    if (wdt != ESP_OK) {
        ESP_LOGE(TAG, "esp_task_wdt_add: %s — encoders працює без сторожового таймера", esp_err_to_name(wdt));
    }
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(ENC_PERIOD_MS));

        bool avail[ENC_WHEELS];
        portENTER_CRITICAL(&s_mux);
        for (int i = 0; i < ENC_WHEELS; i++) avail[i] = s_state.w[i].available;
        portEXIT_CRITICAL(&s_mux);

        int32_t raw[ENC_WHEELS] = { 0 };
        esp_err_t read_err[ENC_WHEELS] = { ESP_OK, ESP_OK, ESP_OK, ESP_OK };
        for (int i = 0; i < ENC_WHEELS; i++) {
            if (!avail[i]) continue;
            int v = 0;
            read_err[i] = pcnt_unit_get_count(s_unit[i], &v);
            raw[i] = (int32_t)v;
        }
        uint64_t now_us = (uint64_t)esp_timer_get_time();

        uint64_t gap_us = 0;
        portENTER_CRITICAL(&s_mux);
        for (int i = 0; i < ENC_WHEELS; i++) {
            if (read_err[i] != ESP_OK) enc_core_set_unavailable(&s_state, (enc_wheel_t)i, esp_err_to_name(read_err[i]));
        }
        if (s_state.primed) gap_us = now_us - s_state.last_us;
        enc_core_sample(&s_state, raw, now_us);
        portEXIT_CRITICAL(&s_mux);

        for (int i = 0; i < ENC_WHEELS; i++) {
            if (read_err[i] != ESP_OK) {
                ESP_LOGE(TAG, "енкодер %s: читання лічильника %s — колесо недоступне", s_names[i], esp_err_to_name(read_err[i]));
            }
        }
        if (gap_us > ENC_GAP_LOG_US) {
            // Діагностика пропуску вибірок (sad.md §4, §11): значення не відкидаються.
            ESP_LOGW(TAG, "пропуск вибірок енкодерів: %llu мс між вибірками", (unsigned long long)(gap_us / 1000u));
        }
        if (wdt == ESP_OK) {
            esp_task_wdt_reset();
        }
    }
}

esp_err_t encoders_start(void)
{
    enc_state_t init;
    enc_core_init(&init);
    for (int i = 0; i < ENC_WHEELS; i++) {
        esp_err_t err = setup_wheel(i);
        if (err != ESP_OK) {
            enc_core_set_unavailable(&init, (enc_wheel_t)i, esp_err_to_name(err));
            ESP_LOGE(TAG, "енкодер %s недоступний: %s", s_names[i], esp_err_to_name(err));
        }
    }
    esp_err_t result = ESP_OK;
    if (enc_core_available_count(&init) == 0) {
        result = ESP_FAIL;
    } else if (xTaskCreatePinnedToCore(encoders_task, "encoders", ENC_STACK, NULL, ENC_PRIORITY, NULL, ENC_CORE) != pdPASS) {
        for (int i = 0; i < ENC_WHEELS; i++) {
            enc_core_set_unavailable(&init, (enc_wheel_t)i, "задача encoders не запущена");
        }
        ESP_LOGE(TAG, "задача encoders не запущена — енкодери недоступні, ровер працює далі");
        result = ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&s_mux);
    s_state = init;
    portEXIT_CRITICAL(&s_mux);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "енкодери: %u з %d доступні, вибірка кожні %d мс", enc_core_available_count(&init), ENC_WHEELS,
                 ENC_PERIOD_MS);
    }
    return result;
}

void encoders_state_get(enc_state_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_state;
    portEXIT_CRITICAL(&s_mux);
}

size_t encoders_snapshot_text(char *buf, size_t n)
{
    enc_state_t copy;
    encoders_state_get(&copy);
    return enc_core_format_snapshot(&copy, buf, n);
}

size_t encoders_reset_text(char *buf, size_t n)
{
    enc_state_t copy;
    portENTER_CRITICAL(&s_mux);
    enc_core_reset(&s_state);
    copy = s_state;
    portEXIT_CRITICAL(&s_mux);
    return enc_core_format_reset(&copy, buf, n);
}

size_t encoders_status_text(char *buf, size_t n)
{
    enc_state_t copy;
    encoders_state_get(&copy);
    return enc_core_format_status(&copy, buf, n);
}
