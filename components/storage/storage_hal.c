// HAL сховища: виклики nvs_flash, рішення — у storage_core.c.
#include "storage_hal.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "storage";

static volatile esp_err_t s_last_error = ESP_OK;
static volatile storage_status_t s_status = STORAGE_STATUS_UNAVAILABLE;
static volatile bool s_known;  // виставляється ПІСЛЯ s_status і s_last_error

static storage_result_t to_result(esp_err_t err)
{
    switch (err) {
    case ESP_OK:                          return STORAGE_RESULT_OK;
    case ESP_ERR_NVS_NO_FREE_PAGES:       return STORAGE_RESULT_NO_FREE_PAGES;
    case ESP_ERR_NVS_NEW_VERSION_FOUND:   return STORAGE_RESULT_NEW_VERSION;
    default:                              return STORAGE_RESULT_OTHER;
    }
}

storage_status_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    storage_result_t first = to_result(err);
    storage_result_t retry = STORAGE_RESULT_OTHER;

    if (storage_decide(first) == STORAGE_ACTION_ERASE_AND_RETRY) {
        ESP_LOGW(TAG, "сховище пошкоджене або іншої версії (%s) — очищаю", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
        retry = to_result(err);
    }

    s_last_error = err;
    storage_status_t status = storage_final_status(first, retry);
    s_status = status;
    s_known = true;
    if (status == STORAGE_STATUS_UNAVAILABLE) {
        ESP_LOGE(TAG, "ініціалізація сховища невдала: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "%s", storage_status_text(status));
    }
    return status;
}

void storage_status_report(char *buf, size_t n)
{
    if (!s_known) {
        snprintf(buf, n, "ще не ініціалізовано");
    } else if (s_status == STORAGE_STATUS_UNAVAILABLE) {
        snprintf(buf, n, "%s: %s", storage_status_text(s_status), esp_err_to_name(s_last_error));
    } else {
        snprintf(buf, n, "%s", storage_status_text(s_status));
    }
}

esp_err_t storage_last_error(void)
{
    return s_last_error;
}
