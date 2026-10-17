// HAL сторожового таймера: esp_task_wdt_reconfigure з panic, задача heartbeat, причина скидання.
// Рішення «годувати чи ні» приймає watchdog_core.
#include "watchdog_hal.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "watchdog.h"

#define HEARTBEAT_PERIOD_MS 1000   // набагато менше за таймаут Task WDT
#define HEARTBEAT_STACK 3072
#define HEARTBEAT_PRIORITY 2
#define HEARTBEAT_CORE 0

static const char *TAG = "watchdog";

static watchdog_core_t s_core;
static bool s_started;

// Маска ядер, чиї idle-задачі стежить TWDT, — як у Kconfig (sdkconfig.defaults не чіпаємо).
static uint32_t idle_core_mask(void)
{
    uint32_t mask = 0;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    mask |= (1U << 0);
#endif
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
    mask |= (1U << 1);
#endif
    return mask;
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "heartbeat не підписано на Task WDT: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (watchdog_should_feed(&s_core)) {
            esp_task_wdt_reset();
        } else {
            ESP_LOGW(TAG, "heartbeat зупинено: сторожовий таймер спрацює через ~%d с",
                     CONFIG_ESP_TASK_WDT_TIMEOUT_S);
            vTaskSuspend(NULL);  // більше не годуємо
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}

esp_err_t watchdog_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    watchdog_core_init(&s_core);

    const esp_task_wdt_config_t cfg = {
        .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = idle_core_mask(),
        .trigger_panic = true,  // спрацювання → panic → перезавантаження з причиною TASK_WDT
    };
    esp_err_t err = esp_task_wdt_reconfigure(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_task_wdt_reconfigure: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreatePinnedToCore(heartbeat_task, "heartbeat", HEARTBEAT_STACK, NULL,
                                HEARTBEAT_PRIORITY, NULL, HEARTBEAT_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу heartbeat");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "Task WDT: %d с, panic увімкнено, heartbeat запущено", CONFIG_ESP_TASK_WDT_TIMEOUT_S);
    return ESP_OK;
}

void watchdog_request_hang(void)
{
    watchdog_core_request_hang(&s_core);
}

const char *watchdog_last_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_USB:       return "USB";
    default:                return "UNKNOWN";
    }
}
