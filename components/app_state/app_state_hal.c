// HAL автомата станів: захист мʼютексом m_state, відмітка часу, друк у журнал.
#include "app_state_hal.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_state";

static app_sm_t s_sm;
static SemaphoreHandle_t m_state;

static void log_refusal(const char *msg)
{
    ESP_LOGW(TAG, "%s", msg);
}

esp_err_t app_state_hal_init(void)
{
    if (m_state == NULL) {
        m_state = xSemaphoreCreateMutex();
        if (m_state == NULL) {
            ESP_LOGE(TAG, "не вдалося створити мʼютекс m_state");
            return ESP_ERR_NO_MEM;
        }
    }
    app_sm_init(&s_sm, log_refusal);
    ESP_LOGI(TAG, "стан: %s", app_state_name(app_sm_state(&s_sm)));
    return ESP_OK;
}

app_sm_result_t app_state_request(app_state_t to)
{
    xSemaphoreTake(m_state, portMAX_DELAY);
    app_state_t from = app_sm_state(&s_sm);
    uint32_t t_ms = (uint32_t)(esp_timer_get_time() / 1000);
    app_sm_result_t r = app_sm_request(&s_sm, to, t_ms);
    xSemaphoreGive(m_state);
    if (r == APP_SM_OK) {
        ESP_LOGI(TAG, "перехід: %s -> %s", app_state_name(from), app_state_name(to));
    }
    return r;
}

app_state_t app_state_get(void)
{
    xSemaphoreTake(m_state, portMAX_DELAY);
    app_state_t s = app_sm_state(&s_sm);
    xSemaphoreGive(m_state);
    return s;
}

size_t app_state_history(app_sm_entry_t *out, size_t max)
{
    xSemaphoreTake(m_state, portMAX_DELAY);
    size_t n = app_sm_history_count(&s_sm);
    if (n > max) {
        n = max;
    }
    for (size_t i = 0; i < n; i++) {
        app_sm_history_get(&s_sm, i, &out[i]);
    }
    xSemaphoreGive(m_state);
    return n;
}
