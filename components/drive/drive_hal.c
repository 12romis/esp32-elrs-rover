// HAL руху: виконує рішення drive_core. Єдиний власник ліній моторів (ADR-0001).
#include "drive_hal.h"

#include "app_state_hal.h"
#include "battery_hal.h"
#include "board_pins.h"
#include "crsf_hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define CONTROL_STACK 4096
#define CONTROL_PRIORITY 5
#define CONTROL_CORE 1
#define CONTROL_PERIOD_MS 20
#define REQUEST_MAX_AGE_MS 150   // запит старший за це control відкидає; менший за REPLY_TIMEOUT_MS із запасом (ADR-0001)
#define REPLY_TIMEOUT_MS 200
#define QUEUE_LEN 4

#define PWM_FREQ_HZ 20000
#define PWM_RES LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX 1023
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_MOTORS LEDC_TIMER_0
#define LEDC_CH_LEFT LEDC_CHANNEL_0
#define LEDC_CH_RIGHT LEDC_CHANNEL_1

static const char *TAG = "drive";

typedef struct {
    drive_req_t req;
    uint32_t t_ms;
    uint32_t seq;
} queued_req_t;

static bool s_init_ok;
static volatile bool s_running;   // true лише після успішної підписки control на Task WDT (ADR-0004)
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_submit_lock;
static SemaphoreHandle_t s_reply_sem;
static drive_reply_t s_reply;
static uint32_t s_reply_seq;
static uint32_t s_next_seq;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static drive_status_t s_status;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void gpio_out_low(int pin)
{
    gpio_set_level(pin, 0);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 0);
}

static void set_duty(ledc_channel_t ch, uint32_t permille)
{
    uint32_t duty = permille * PWM_DUTY_MAX / 1000;
    ledc_set_duty(LEDC_MODE, ch, duty);
    ledc_update_duty(LEDC_MODE, ch);
}

// Застосовує виходи. Вимкнення: ШІМ і напрямок, потім STBY; ввімкнення: напрямок і ШІМ, STBY останньою.
static void apply_outputs(const drive_outputs_t *o)
{
    if (!o->stby) {
        set_duty(LEDC_CH_LEFT, 0);
        set_duty(LEDC_CH_RIGHT, 0);
        gpio_set_level(PIN_DIR_L1, 0);
        gpio_set_level(PIN_DIR_L2, 0);
        gpio_set_level(PIN_DIR_R1, 0);
        gpio_set_level(PIN_DIR_R2, 0);
        gpio_set_level(PIN_STBY, 0);
        return;
    }
    gpio_set_level(PIN_DIR_L1, o->left.dir1);
    gpio_set_level(PIN_DIR_L2, o->left.dir2);
    gpio_set_level(PIN_DIR_R1, o->right.dir1);
    gpio_set_level(PIN_DIR_R2, o->right.dir2);
    set_duty(LEDC_CH_LEFT, o->left.duty_permille);
    set_duty(LEDC_CH_RIGHT, o->right.duty_permille);
    gpio_set_level(PIN_STBY, 1);
}

esp_err_t drive_hal_init(void)
{
    // Лінії напрямку й STBY — виходи з низьким рівнем (STBY до цього тримала притяжка R16).
    gpio_out_low(PIN_STBY);
    gpio_out_low(PIN_DIR_L1);
    gpio_out_low(PIN_DIR_L2);
    gpio_out_low(PIN_DIR_R1);
    gpio_out_low(PIN_DIR_R2);

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER_MOTORS,
        .duty_resolution = PWM_RES,
        .freq_hz = PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %s — моторів не запущено", esp_err_to_name(err));
        return err;
    }
    ledc_channel_config_t left = {
        .gpio_num = PIN_PWM_L,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CH_LEFT,
        .timer_sel = LEDC_TIMER_MOTORS,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config_t right = left;
    right.gpio_num = PIN_PWM_R;
    right.channel = LEDC_CH_RIGHT;
    err = ledc_channel_config(&left);
    if (err == ESP_OK) {
        err = ledc_channel_config(&right);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config: %s — моторів не запущено", esp_err_to_name(err));
        return err;
    }
    drive_outputs_t off = drive_outputs_off();
    apply_outputs(&off);
    s_init_ok = true;
    ESP_LOGI(TAG, "лінії моторів у вимкненому стані (ШІМ 20 кГц, 10 біт)");
    return ESP_OK;
}

bool drive_hal_running(void)
{
    return s_running;
}

void drive_hal_status(drive_status_t *s)
{
    portENTER_CRITICAL(&s_lock);
    *s = s_status;
    portEXIT_CRITICAL(&s_lock);
}

bool drive_hal_submit(const drive_req_t *req, drive_reply_t *reply)
{
    if (!s_running) {
        return false;
    }
    xSemaphoreTake(s_submit_lock, portMAX_DELAY);
    while (xSemaphoreTake(s_reply_sem, 0) == pdTRUE) {
    }  // прибрати прострочену відповідь попереднього запиту
    queued_req_t item = {.req = *req, .t_ms = now_ms(), .seq = ++s_next_seq};
    bool ok = false;
    if (xQueueSend(s_queue, &item, 0) == pdTRUE) {
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(REPLY_TIMEOUT_MS);
        for (;;) {
            TickType_t now = xTaskGetTickCount();
            if ((int32_t)(deadline - now) <= 0 ||
                xSemaphoreTake(s_reply_sem, deadline - now) != pdTRUE) {
                break;
            }
            if (s_reply_seq == item.seq) {
                *reply = s_reply;
                ok = true;
                break;
            }
        }
    }
    xSemaphoreGive(s_submit_lock);
    return ok;
}

static void log_stop(const drive_ctl_out_t *o, const drive_ctl_in_t *in, uint32_t now)
{
    switch (o->stop) {
    case DRIVE_STOP_WINDOW_EXPIRED:
        ESP_LOGI(TAG, "команда руху спливла — мотори вимкнено, ровер роззброєно; мінімальна напруга за час руху: %u мВ",
                 (unsigned)o->min_mv);
        break;
    case DRIVE_STOP_LOW_VOLTAGE:
        ESP_LOGW(TAG, "напруга %u мВ нижча за поріг — мотори вимкнено, FAILSAFE (вихід лише апаратним скиданням)",
                 (unsigned)o->mv);
        break;
    case DRIVE_STOP_STALE_VOLTAGE:
        ESP_LOGW(TAG, "вимірювання напруги застаріло або відсутнє — мотори вимкнено, FAILSAFE");
        break;
    case DRIVE_STOP_DISARM:
        if (in->state == APP_STATE_ARMED) {
            ESP_LOGI(TAG, "роззброєно — мотори вимкнено");
        }
        break;
    case DRIVE_STOP_LINK_LOST:
        // Мітка останнього кадру й мітка переходу — для перевірки ≤ 520 мс під час приймання (spec §6).
        ESP_LOGW(TAG, "%s — мотори вимкнено, FAILSAFE (причина: втрата звʼязку); останній кадр: %u мс, перехід: %u мс",
                 drive_stop_text(o->stop), (unsigned)in->rc.t_ms, (unsigned)now);
        break;
    case DRIVE_STOP_REMOTE_APPEARED:
    case DRIVE_STOP_SWITCH_DISARM:
        ESP_LOGI(TAG, "%s", drive_stop_text(o->stop));
        break;
    case DRIVE_STOP_NONE:
        break;
    }
}

// Події кроку для журналу: по одному рядку на подію, не на такт (contracts/cli.md, «Журнал без команд»).
static void log_events(const drive_ctl_out_t *o, const drive_ctl_in_t *in, uint32_t now)
{
    if (o->events & DRIVE_EVT_REMOTE_APPEARED) {
        ESP_LOGI(TAG, "з'явився пульт — стендовий режим закінчено; консольні arm і drive вимкнено до перезапуску плати");
    }
    if ((o->events & DRIVE_EVT_LINK_LOST) && o->stop != DRIVE_STOP_LINK_LOST) {
        ESP_LOGW(TAG, "звʼязок з пультом втрачено (%u мс від останнього кадру)", (unsigned)(now - in->rc.t_ms));
    }
    if (o->events & DRIVE_EVT_LINK_RESTORED) {
        ESP_LOGI(TAG, "звʼязок з пультом відновлено");
    }
    if (o->events & DRIVE_EVT_REMOTE_ARMED) {
        ESP_LOGI(TAG, "озброєння з пульта: тумблер перемкнуто в ARM (джерело: пульт)");
    }
    if (o->events & DRIVE_EVT_ARM_REFUSED) {
        ESP_LOGW(TAG, "озброєння з пульта відхилено: %s", drive_block_text(o->arm_block));
    }
    if (o->events & DRIVE_EVT_FAILSAFE_EXIT) {
        ESP_LOGI(TAG, "вихід із FAILSAFE: звʼязок стабільний, тумблер у DISARM");
    }
}

static void control_task(void *arg)
{
    (void)arg;
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        // Без Task WDT зависання control лишило б мотори без захисту (ADR-0004): консоль не приймає запитів (s_running),
        // а крок керування з outputs_inhibited не озброює з пульта й тримає виходи вимкненими (review F1).
        ESP_LOGE(TAG, "esp_task_wdt_add: %s — моторів не запущено, озброєння з консолі й пульта недоступне", esp_err_to_name(err));
    } else {
        s_running = true;
    }
    drive_ctl_t ctl;
    drive_ctl_init(&ctl);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        queued_req_t item;
        drive_ctl_in_t in = {0};
        bool have_req = false;
        uint32_t seq = 0;
        bool got = xQueueReceive(s_queue, &item, 0) == pdTRUE;
        in.state = app_state_get();
        in.failsafe_cause = app_state_failsafe_cause();
        in.outputs_inhibited = !s_running;
        battery_get_snapshot(&in.battery);
        crsf_snapshot_get(&in.rc);   // без приймача знімок порожній: стендовий режим (ADR-0001)
        uint32_t now = now_ms();  // час беремо ПІСЛЯ зчитування черги й знімка: вік не стає відʼємним
        in.now_ms = now;
        if (got && (int32_t)(now - item.t_ms) <= REQUEST_MAX_AGE_MS) {
            in.req = item.req;
            have_req = true;
            seq = item.seq;
        }

        drive_ctl_out_t out;
        drive_ctl_step(&ctl, &in, &out);

        apply_outputs(&out.out);  // спершу виходи (ADR-0002)
        bool transition_ok = true;
        if (out.has_state_request) {
            if (out.state_request == APP_STATE_FAILSAFE) {
                // Вхід у FAILSAFE завжди з причиною; повторне повідомлення «напруга» підвищує причину (ADR-0002).
                transition_ok = app_state_enter_failsafe(out.failsafe_cause) == APP_SM_OK;
            } else {
                transition_ok = app_state_request(out.state_request) == APP_SM_OK;
            }
        }

        // Знімок стану керування з пульта для status: тяга, поворот, тумблер, що блокує озброєння, вихід з FAILSAFE.
        crsf_intent_t intent;
        crsf_intent(&in.rc, &intent);
        drive_block_t blk = drive_arm_block(&ctl, in.state, &in.battery, &intent, now);
        drive_fsx_t fsx = drive_failsafe_exit_state(&ctl, in.failsafe_cause, &intent, now);

        portENTER_CRITICAL(&s_lock);
        s_status.stby = out.out.stby;
        s_status.left_permille = out.left_permille;
        s_status.right_permille = out.right_permille;
        s_status.remaining_ms = out.remaining_ms;
        s_status.remote_active = ctl.remote_active;
        s_status.link_alive = ctl.link_alive;
        s_status.since_last_ms = in.rc.have_frame ? (uint32_t)(now - in.rc.t_ms) : 0;
        s_status.intent = intent;
        s_status.sw_shown = drive_switch_shown(&ctl);
        s_status.arm_block = blk;
        s_status.failsafe_cause = in.failsafe_cause;
        s_status.fsx = fsx;
        if (out.stop != DRIVE_STOP_NONE && !(out.stop == DRIVE_STOP_DISARM && in.state != APP_STATE_ARMED)) {
            s_status.last_stop = out.stop;  // зупинка справжня: запам'ятати для status (AC-12)
            s_status.last_stop_min_mv = out.min_mv;
            s_status.last_stop_t_ms = now;
        }
        portEXIT_CRITICAL(&s_lock);

        if (have_req) {
            bool accepted = out.accepted && transition_ok;
            s_reply = (drive_reply_t){.accepted = accepted,
                                      .reason = (out.accepted && !transition_ok) ? DRIVE_REASON_TRANSITION_REFUSED : out.reason,
                                      .state_before = in.state,
                                      .has_state_request = out.has_state_request,
                                      .state_request = out.state_request,
                                      .stop = out.stop,
                                      .mv = out.mv,
                                      .left_permille = out.left_permille,
                                      .right_permille = out.right_permille,
                                      .failsafe_cause = in.failsafe_cause,
                                      .fsx = fsx,
                                      .switch_arm = intent.sw == CRSF_SW_ARM};
            s_reply_seq = seq;
            xSemaphoreGive(s_reply_sem);
        }
        log_stop(&out, &in, now);  // лог — після вимкнення виходів і запиту стану
        log_events(&out, &in, now);

        esp_task_wdt_reset();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t drive_start(void)
{
    if (!s_init_ok) {
        ESP_LOGE(TAG, "drive_hal_init не пройшла — control не стартує, моторів не запущено");
        return ESP_ERR_INVALID_STATE;
    }
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(queued_req_t));
    s_submit_lock = xSemaphoreCreateMutex();
    s_reply_sem = xSemaphoreCreateBinary();
    if (s_queue == NULL || s_submit_lock == NULL || s_reply_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(control_task, "control", CONTROL_STACK, NULL, CONTROL_PRIORITY, NULL,
                                CONTROL_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу control");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;  // s_running виставляє сама задача після підписки на Task WDT
}
