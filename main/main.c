// Фаза 0: старт у відомому стані. Тільки ініціалізація й старт задач, без логіки.
// Мотори, ШІМ, STBY і шина периферії не задіяні (ADR-0003).
#include <stdio.h>

#include "app_state_hal.h"
#include "console.h"
#include "esp_log.h"
#include "storage_hal.h"
#include "watchdog_hal.h"

static const char *TAG = "rover";

static storage_status_t s_storage_status;
static bool s_storage_known;

// Текст стану сховища для команди status (з причиною, якщо сховище недоступне).
static const char *storage_status_report(void)
{
    static char buf[64];
    if (!s_storage_known) {
        return "ще не ініціалізовано";
    }
    if (s_storage_status == STORAGE_STATUS_UNAVAILABLE) {
        snprintf(buf, sizeof(buf), "%s: %s", storage_status_text(s_storage_status),
                 esp_err_to_name(storage_last_error()));
        return buf;
    }
    return storage_status_text(s_storage_status);
}

static const console_ops_t s_console_ops = {
    .storage_status = storage_status_report,
    .request_hang = watchdog_request_hang,
    .reset_reason = watchdog_last_reset_reason,
    .reboot = NULL,  // esp_restart() усередині консолі
};

void app_main(void)
{
    if (app_state_hal_init() != ESP_OK) {
        ESP_LOGE(TAG, "автомат станів не створено — старт неможливий");
        return;
    }
    ESP_LOGI(TAG, "причина останнього скидання: %s", watchdog_last_reset_reason());

    // Захист і консоль стартують до сховища: збій сховища має бути видимий і діагностований (ADR-0004).
    esp_err_t wdt = watchdog_start();
    esp_err_t con = console_start(&s_console_ops);
    s_storage_status = storage_init();
    s_storage_known = true;

    if (wdt != ESP_OK || con != ESP_OK || s_storage_status == STORAGE_STATUS_UNAVAILABLE) {
        ESP_LOGE(TAG, "старт не завершено (watchdog=%s, консоль=%s, %s) — стан лишається BOOT",
                 esp_err_to_name(wdt), esp_err_to_name(con), storage_status_report());
        return;
    }

    // SELFTEST у фазі 0 — заглушка без перевірок і завжди успішна.
    app_state_request(APP_STATE_SELFTEST);
    app_state_request(APP_STATE_DISARMED);
}
