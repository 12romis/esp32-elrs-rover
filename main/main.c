// Фаза 1: старт у відомому стані й керування моторами з консолі. Тільки ініціалізація, старт задач і колбеки, без логіки.
// Порядок старту (sad.md §5): виходи моторів вимкнено першими, потім нуль у розширювач (перша операція на шині), захист, консоль,
// вимірювання напруги, control, ui, сховище.
#include <stdio.h>
#include <string.h>

#include "app_state_hal.h"
#include "battery_hal.h"
#include "console.h"
#include "crsf_hal.h"
#include "drive_hal.h"
#include "ioexp_hal.h"
#include "esp_log.h"
#include "storage_hal.h"
#include "ui_hal.h"
#include "watchdog_hal.h"

static const char *TAG = "rover";

// Колбек консолі: arm, disarm, drive через чергу control. Консоль не залежить від drive (колбеки передає main).
static bool motor_cmd(console_motor_cmd_t cmd, float throttle, float steer, console_motor_reply_t *reply)
{
    if (!drive_hal_running()) {
        if (cmd == CONSOLE_MOTOR_DISARM) {  // control не стартувала: виходи ніколи не вмикалися
            reply->accepted = true;
            snprintf(reply->text, sizeof(reply->text), "моторів не запущено, керування не стартувало");
            return true;
        }
        return false;
    }
    drive_req_t req = {.kind = cmd == CONSOLE_MOTOR_ARM ? DRIVE_REQ_ARM
                              : cmd == CONSOLE_MOTOR_DISARM ? DRIVE_REQ_DISARM : DRIVE_REQ_DRIVE,
                       .throttle = throttle,
                       .steer = steer};
    drive_reply_t r;
    if (!drive_hal_submit(&req, &r)) {
        return false;
    }
    reply->accepted = r.accepted;
    reply->left_permille = r.left_permille;
    reply->right_permille = r.right_permille;
    if (!r.accepted) {
        drive_reason_text(r.reason, r.mv, reply->text, sizeof(reply->text));
    } else if (cmd == CONSOLE_MOTOR_DISARM) {
        drive_disarm_text(r.state_before, r.has_state_request, r.state_request, r.stop, reply->text, sizeof(reply->text));
    } else {
        reply->text[0] = '\0';
    }
    return true;
}

static void motor_status(console_motor_status_t *s)
{
    drive_status_t d;
    drive_hal_status(&d);
    battery_snapshot_t b;
    battery_get_snapshot(&b);
    s->mv = b.mv;
    s->mv_valid = b.valid;
    s->stby = d.stby;
    s->left_permille = d.left_permille;
    s->right_permille = d.right_permille;
    s->remaining_ms = d.remaining_ms;
    s->last_stop_reason = d.last_stop == DRIVE_STOP_NONE ? NULL : drive_stop_text(d.last_stop);
    s->last_stop_min_mv = d.last_stop_min_mv;
    s->last_stop_t_ms = d.last_stop_t_ms;
}

// Колбеки світла й зумера: консоль не залежить від ui, дії передає main.
static bool light_cmd(ui_channel_t ch, bool on, console_ui_reply_t *reply)
{
    if (!ui_hal_running()) {
        return false;
    }
    ui_hal_reply_t r;
    ui_hal_light(ch, on, &r);
    *reply = (console_ui_reply_t){.result = r.result, .bus_failure = r.bus_failure, .queued = r.queued,
                                  .head = r.head, .red = r.red, .green = r.green};
    return true;
}

static bool beep_cmd(uint32_t hz, uint32_t dur_ms, console_ui_reply_t *reply)
{
    if (!ui_hal_running()) {
        return false;
    }
    ui_hal_reply_t r;
    ui_hal_beep(hz, dur_ms, &r);
    *reply = (console_ui_reply_t){.result = r.result, .bus_failure = r.bus_failure, .queued = r.queued};
    return true;
}

static void ui_status(console_ui_status_t *s)
{
    ui_hal_status_t u;
    ui_hal_status(&u);
    *s = (console_ui_status_t){.expander_ok = u.expander_ok, .head = u.head, .red = u.red, .green = u.green,
                               .rear_auto = u.rear_auto, .indicator_red = u.indicator_red, .buzzer_on = u.buzzer_on,
                               .remaining_ms = u.remaining_ms};
}

static const console_ops_t s_console_ops = {
    .storage_status = storage_status_report,
    .request_hang = watchdog_request_hang,
    .reset_reason = watchdog_last_reset_reason,
    .reboot = NULL,  // esp_restart() усередині консолі
    .motor_cmd = motor_cmd,
    .motor_status = motor_status,
    .light_cmd = light_cmd,
    .beep_cmd = beep_cmd,
    .ui_status = ui_status,
};

void app_main(void)
{
    if (app_state_hal_init() != ESP_OK) {
        ESP_LOGE(TAG, "автомат станів не створено — старт неможливий");
        return;
    }
    // Лінії моторів — у вимкнений стан першими (до цього STBY тримає апаратна притяжка R16).
    esp_err_t drv = drive_hal_init();
    // Перша операція на шині — запис нуля в розширювач (конституція, п. 7): гасить світло, тримає далекоміри в скиданні.
    // Відмова не зупиняє старт: світло недоступне, мотори, консоль і зумер працюють (ADR-0003).
    esp_err_t iox = ioexp_start();
    ESP_LOGI(TAG, "причина останнього скидання: %s", watchdog_last_reset_reason());

    // Захист і консоль стартують до сховища: збій сховища має бути видимий і діагностований (ADR-0004 фази 0).
    esp_err_t wdt = watchdog_start();
    esp_err_t con = console_start(&s_console_ops);
    esp_err_t bat = battery_start();
    // control стартує до storage_init, тож disarm працює і в BOOT, якщо сховище не піднялося.
    esp_err_t ctl = drv == ESP_OK ? drive_start() : drv;
    // Приймач пульта (ADR-0001): після control, щоб знімок читався з першого такту. Збій UART не зупиняє старт: без
    // кадрів ровер лишається в стендовому режимі, консольні arm і drive працюють як у фазі 1.
    esp_err_t rcv = crsf_start();
    if (rcv != ESP_OK) {
        ESP_LOGW(TAG, "приймач CRSF не стартував (%s) — стендовий режим, керування лише з консолі", esp_err_to_name(rcv));
    }
    esp_err_t ui = ui_start();  // ui лише читає стан і борти, мотори від неї не залежать
    storage_status_t storage = storage_init();

    if (wdt != ESP_OK || con != ESP_OK || storage == STORAGE_STATUS_UNAVAILABLE) {
        char report[80];
        storage_status_report(report, sizeof(report));
        ESP_LOGE(TAG, "старт не завершено (watchdog=%s, консоль=%s, %s) — стан лишається BOOT",
                 esp_err_to_name(wdt), esp_err_to_name(con), report);
        return;
    }
    if (iox != ESP_OK || ui != ESP_OK) {
        ESP_LOGW(TAG, "світло недоступне або ui не стартувала (ioexp=%s, ui=%s) — решта працює", esp_err_to_name(iox),
                 esp_err_to_name(ui));
    }
    if (bat != ESP_OK || ctl != ESP_OK) {
        // Моторів не запущено: control не працює або напруга не вимірюється; консоль і автомат працюють, arm відхиляється.
        ESP_LOGE(TAG, "керування моторами недоступне (battery=%s, control=%s) — моторів не запущено",
                 esp_err_to_name(bat), esp_err_to_name(ctl));
    }

    // SELFTEST у фазі 1 лишається заглушкою без перевірок і завжди успішною; слабка батарея лише блокує arm.
    app_state_request(APP_STATE_SELFTEST);
    app_state_request(APP_STATE_DISARMED);
}
