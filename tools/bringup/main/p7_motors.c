// P7 — мотори на мінімальному ШІМ і знак енкодера. ВИКИДНИЙ ЗОНД, не фаза 1:
// тут свідомо немає arm/disarm, failsafe, мікшера й відсічки по напрузі.
// Залізо: усе. КОЛЕСА В ПОВІТРІ, шасі на підставці.
// Заповнює ENC_DIR_SIGN[4] і підтверджує прив'язку з §2.2 (U5: AO→M4, BO→M3).
//
// Блокер (конституція, п. 12; док. 02 §10.4): мотори підключати лише після прозвонювання
// розпіновки шлейфів. Доки P7_HARNESS_VERIFIED = 0, тест нічого не вмикає.
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

#define P7_HARNESS_VERIFIED  0     // поставити 1 ТІЛЬКИ після прозвонки шлейфів
#define P7_ENCODER_PULLUP    0     // підставити результат P6 (ENC_NEEDS_PULLUP)
#define P7_DUTY_PERCENT      25    // фіксовано, не підвищувати
#define P7_RUN_MS            2000

#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_CH_LEFT     LEDC_CHANNEL_0
#define LEDC_CH_RIGHT    LEDC_CHANNEL_1
#define DUTY_MAX         1023      // 10 біт

static const char *TAG = "bringup-p7";

static void outputs_safe(void)
{
    gpio_set_level(PIN_STBY, 0);
    gpio_set_level(PIN_DIR_L1, 0);
    gpio_set_level(PIN_DIR_L2, 0);
    gpio_set_level(PIN_DIR_R1, 0);
    gpio_set_level(PIN_DIR_R2, 0);
}

static void set_duty(ledc_channel_t ch, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ch, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ch));
}

static void gpio_out(int pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(pin, 0);
}

// side: 0 = лівий борт, 1 = правий; forward: 1 = вперед
static void run_side(int side, int forward)
{
    int before[4], after[4];
    for (int i = 0; i < 4; i++) {
        before[i] = enc_count(i);
    }

    int d1 = side ? PIN_DIR_R1 : PIN_DIR_L1;
    int d2 = side ? PIN_DIR_R2 : PIN_DIR_L2;
    ledc_channel_t ch = side ? LEDC_CH_RIGHT : LEDC_CH_LEFT;

    ESP_LOGW(TAG, "P7 >>> %s борт, %s, %d%% ШІМ, %d мс", side ? "ПРАВИЙ" : "ЛІВИЙ",
             forward ? "ВПЕРЕД" : "НАЗАД", P7_DUTY_PERCENT, P7_RUN_MS);

    gpio_set_level(d1, forward ? 1 : 0);
    gpio_set_level(d2, forward ? 0 : 1);
    set_duty(ch, DUTY_MAX * P7_DUTY_PERCENT / 100);
    gpio_set_level(PIN_STBY, 1);          // STBY піднімається останнім
    vTaskDelay(pdMS_TO_TICKS(P7_RUN_MS));
    set_duty(ch, 0);
    outputs_safe();                       // STBY вниз, DIR у нуль

    vTaskDelay(pdMS_TO_TICKS(300));       // дати колесу зупинитись
    for (int i = 0; i < 4; i++) {
        after[i] = enc_count(i);
    }
    ESP_LOGI(TAG, "P7 delta: FL=%+d FR=%+d RL=%+d RR=%+d",
             after[0] - before[0], after[1] - before[1],
             after[2] - before[2], after[3] - before[3]);
}

void test_p7_motors(void)
{
    // STBY і DIR у безпечний стан першим ділом (R16 і так тримає STBY внизу)
    gpio_out(PIN_STBY);
    gpio_out(PIN_DIR_L1);
    gpio_out(PIN_DIR_L2);
    gpio_out(PIN_DIR_R1);
    gpio_out(PIN_DIR_R2);
    outputs_safe();

#if !P7_HARNESS_VERIFIED
    ESP_LOGE(TAG, "P7 ВІДМОВА: P7_HARNESS_VERIFIED = 0. Спершу прозвони моторні шлейфи");
    ESP_LOGE(TAG, "(док. 01, розділ 4; конституція, п. 12). Жоден вихід не вмикався");
    return;
#endif

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t l = {
        .gpio_num = PIN_PWM_L, .speed_mode = LEDC_MODE, .channel = LEDC_CH_LEFT,
        .timer_sel = LEDC_TIMER, .duty = 0, .hpoint = 0,
    };
    ledc_channel_config_t r = l;
    r.gpio_num = PIN_PWM_R;
    r.channel = LEDC_CH_RIGHT;
    ESP_ERROR_CHECK(ledc_channel_config(&l));
    ESP_ERROR_CHECK(ledc_channel_config(&r));

    enc_init(P7_ENCODER_PULLUP);

    ESP_LOGW(TAG, "P7 КОЛЕСА В ПОВІТРІ? Старт через 10 с. Відʼєднай USB, щоб скасувати");
    for (int s = 10; s > 0; s--) {
        ESP_LOGW(TAG, "P7 ... %d", s);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    for (;;) {
        run_side(0, 1);
        vTaskDelay(pdMS_TO_TICKS(3000));
        run_side(0, 0);
        vTaskDelay(pdMS_TO_TICKS(3000));
        run_side(1, 1);
        vTaskDelay(pdMS_TO_TICKS(3000));
        run_side(1, 0);
        ESP_LOGI(TAG, "P7 цикл завершено, пауза 10 с. Додатний delta на вперед = ENC_DIR_SIGN +1");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
