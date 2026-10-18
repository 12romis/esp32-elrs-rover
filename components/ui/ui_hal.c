// HAL ui: задача ui, мʼютекс m_ui, зумер на LEDC, порт індикатора плати (ADR-0002, ADR-0004).
#include "ui_hal.h"

#include "app_state_hal.h"
#include "board_pins.h"
#include "drive_hal.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "ioexp_hal.h"
#include "ui_calib.h"

#define UI_STACK 4096
#define UI_PRIORITY 2
#define UI_CORE 0
#define UI_PERIOD_MS 100

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_BUZZER LEDC_TIMER_1          // окремий від моторного таймера 0
#define LEDC_CH_BUZZER LEDC_CHANNEL_2
#define BUZZER_RES LEDC_TIMER_10_BIT
#define BUZZER_DUTY_HALF 512                     // 50 % шкали 10 біт під час звучання
#define BUZZER_IDLE_LEVEL 0                      // спокій — низький рівень, постійної напруги на зумері немає

static const char *TAG = "ui";

static SemaphoreHandle_t s_mutex;
static ui_core_t s_core;
static bool s_buzzer_ok;
static bool s_buzzer_on;
static uint32_t s_buzzer_hz;
static bool s_running;
static bool s_indicator_ok;
static bool s_indicator_red;
static rmt_channel_handle_t s_rmt;
static rmt_encoder_handle_t s_rmt_enc;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------- зумер ----------

static esp_err_t buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER_BUZZER,
        .duty_resolution = BUZZER_RES,
        .freq_hz = UI_BEEP_FREQ_MIN_HZ,  // початкове значення з діапазону зумера; до першого звуку вихід у спокої
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }
    ledc_channel_config_t ch = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_BUZZER,
        .timer_sel = LEDC_TIMER_BUZZER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_stop(LEDC_MODE, LEDC_CH_BUZZER, BUZZER_IDLE_LEVEL);
}

static void buzzer_apply(bool on, uint32_t hz)
{
    if (!s_buzzer_ok) {
        return;
    }
    if (on) {
        if (!s_buzzer_on || hz != s_buzzer_hz) {
            if (ledc_set_freq(LEDC_MODE, LEDC_TIMER_BUZZER, hz) != ESP_OK) {
                ESP_LOGW(TAG, "ledc_set_freq(%u Гц) не вдалося — зумер не змінюється", (unsigned)hz);
                return;
            }
            ledc_set_duty(LEDC_MODE, LEDC_CH_BUZZER, BUZZER_DUTY_HALF);
            ledc_update_duty(LEDC_MODE, LEDC_CH_BUZZER);
            s_buzzer_on = true;
            s_buzzer_hz = hz;
        }
    } else if (s_buzzer_on) {
        ledc_stop(LEDC_MODE, LEDC_CH_BUZZER, BUZZER_IDLE_LEVEL);  // нуль і низький рівень, не постійна висока напруга
        s_buzzer_on = false;
    }
}

// ---------- індикатор плати (ADR-0004, ADR-0005): адресований світлодіод, порядок байтів GRB (docs/measured.md, P8) ----------

#define INDICATOR_RMT_HZ 10000000   // 0.1 мкс на такт
#define INDICATOR_TX_TIMEOUT_MS 20  // збій індикатора не затримує нічого

static esp_err_t indicator_init(void)
{
    rmt_tx_channel_config_t cfg = {
        .gpio_num = PIN_STATUS_RGB,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = INDICATOR_RMT_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&cfg, &s_rmt);
    if (err != ESP_OK) {
        return err;
    }
    rmt_bytes_encoder_config_t ecfg = {
        .bit0 = {.level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8},  // 0.4 мкс висока, 0.8 низька
        .bit1 = {.level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 5},  // 0.8 мкс висока, 0.5 низька
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&ecfg, &s_rmt_enc);
    if (err != ESP_OK) {
        return err;
    }
    return rmt_enable(s_rmt);
}

static bool indicator_send(uint8_t g, uint8_t r, uint8_t b)
{
    uint8_t buf[3] = {g, r, b};
    rmt_transmit_config_t tx = {.loop_count = 0};
    return rmt_transmit(s_rmt, s_rmt_enc, buf, sizeof(buf), &tx) == ESP_OK &&
           rmt_tx_wait_all_done(s_rmt, INDICATOR_TX_TIMEOUT_MS) == ESP_OK;
}

static void indicator_set(ui_indicator_t colour)
{
    bool red = colour == UI_IND_RED;
    if (!s_indicator_ok || red == s_indicator_red) {
        return;
    }
    // GRB: червоний — (0, рівень, 0); вимкнено — (0, 0, 0). Невдала передача повториться на наступному такті.
    if (indicator_send(0, red ? UI_INDICATOR_LEVEL : 0, 0)) {
        s_indicator_red = red;
    }
}

// ---------- світло ----------

static uint8_t light_bits(bool head, bool red, bool green)
{
    return (uint8_t)((head ? (1u << IOEXP_BIT_LIGHT_EN) : 0u) | (red ? (1u << IOEXP_BIT_STOP_RED) : 0u) |
                     (green ? (1u << IOEXP_BIT_STOP_GREEN) : 0u));
}

// Бажаний байт: фари ручні; задні ліхтарі — від автоматики в ARMED і FAILSAFE, інакше ручні.
static uint8_t desired_bits(app_state_t st)
{
    bool red = s_core.manual[UI_CH_RED], green = s_core.manual[UI_CH_GREEN];
    if (ui_core_rear_mode(st) == UI_REAR_AUTO) {
        red = s_core.out.red;
        green = s_core.out.green;
    }
    return light_bits(s_core.manual[UI_CH_HEAD], red, green);
}

static void run_tick(void);

// Стан змінився між тактами: спершу обробити перехід, щоб команда не була тихо скасована автоматикою (ADR-0002).
static void sync_state_change(void)
{
    if (!s_core.have_last || app_state_get() != s_core.last_state) {
        run_tick();
    }
}

void ui_hal_light(ui_channel_t ch, bool on, ui_hal_reply_t *reply)
{
    *reply = (ui_hal_reply_t){0};
    if (!s_running) {
        reply->result = UI_DENY_EXPANDER;
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sync_state_change();
    app_state_t st = app_state_get();
    bool prev = (ch >= 0 && ch < UI_CH_COUNT) ? s_core.manual[ch] : false;
    reply->result = ui_core_light(&s_core, ch, on, st, ioexp_hal_available());
    if (reply->result == UI_OK && ioexp_hal_apply(desired_bits(st)) == IOEXP_WR_FAILED) {
        ui_core_rollback(&s_core, ch, prev);  // відхилену команду автоматика ніколи не виконає пізніше
        reply->bus_failure = true;
    }
    reply->head = ioexp_hal_bit(IOEXP_BIT_LIGHT_EN);  // останній підтверджений запис, а не бажаний стан (AC-15)
    reply->red = ioexp_hal_bit(IOEXP_BIT_STOP_RED);
    reply->green = ioexp_hal_bit(IOEXP_BIT_STOP_GREEN);
    xSemaphoreGive(s_mutex);
}

void ui_hal_beep(uint32_t hz, uint32_t dur_ms, ui_hal_reply_t *reply)
{
    *reply = (ui_hal_reply_t){0};
    if (!s_running) {
        reply->result = UI_DENY_STATE;
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sync_state_change();
    reply->result = ui_core_beep(&s_core, hz, dur_ms, now_ms(), app_state_get(), &reply->queued);
    if (reply->result == UI_OK && !reply->queued) {
        buzzer_apply(s_core.out.buzzer_on, s_core.out.buzzer_hz);  // звучить одразу, без очікування такту
    }
    xSemaphoreGive(s_mutex);
}

void ui_hal_status(ui_hal_status_t *s)
{
    *s = (ui_hal_status_t){0};
    if (!s_running) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    app_state_t st = app_state_get();
    s->expander_ok = ioexp_hal_available();
    s->rear_auto = ui_core_rear_mode(st) == UI_REAR_AUTO;
    s->head = ioexp_hal_bit(IOEXP_BIT_LIGHT_EN);  // останній підтверджений запис (AC-13, AC-15); недоступний — вимкнено
    s->red = ioexp_hal_bit(IOEXP_BIT_STOP_RED);
    s->green = ioexp_hal_bit(IOEXP_BIT_STOP_GREEN);
    s->indicator_red = s_core.out.indicator == UI_IND_RED;
    s->buzzer_on = s_core.out.buzzer_on;
    s->remaining_ms = ui_core_remaining_ms(&s_core, now_ms());
    xSemaphoreGive(s_mutex);
}

bool ui_hal_running(void)
{
    return s_running;
}

// ---------- задача ui ----------

// Один такт під мʼютексом m_ui: опитати стан і борти, порахувати, записати розширювач, індикатор і зумер.
static void run_tick(void)
{
    drive_status_t ds;
    drive_hal_status(&ds);
    ui_in_t in = {
        .now_ms = now_ms(),
        .state = app_state_get(),
        .left_permille = ds.left_permille,
        .right_permille = ds.right_permille,
        .expander_ok = ioexp_hal_available(),
    };
    ui_out_t out;
    ui_core_tick(&s_core, &in, &out);
    ioexp_hal_apply(light_bits(out.head, out.red, out.green));  // повтор запису щотакту, доки він не вдасться
    indicator_set(out.indicator);
    buzzer_apply(out.buzzer_on, out.buzzer_hz);
}

static void ui_task(void *arg)
{
    (void)arg;
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        // ui не критична для безпеки (мотори від неї не залежать): працює й без Task WDT, але це видно в журналі.
        ESP_LOGE(TAG, "esp_task_wdt_add: %s — ui працює без сторожового таймера", esp_err_to_name(err));
    }
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        run_tick();
        xSemaphoreGive(s_mutex);

        if (err == ESP_OK) {
            esp_task_wdt_reset();
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

esp_err_t ui_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = buzzer_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "зумер не налаштовано: %s — звуку не буде", esp_err_to_name(err));
    } else {
        s_buzzer_ok = true;
    }
    err = indicator_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "індикатор плати не налаштовано: %s — індикації не буде, решта працює", esp_err_to_name(err));
    } else {
        s_indicator_ok = true;
        indicator_send(0, 0, 0);  // після старту індикатор вимкнений
    }
    ui_core_init(&s_core, UI_SIGNAL_HZ);
    if (xTaskCreatePinnedToCore(ui_task, "ui", UI_STACK, NULL, UI_PRIORITY, NULL, UI_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу ui");
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    return ESP_OK;
}
