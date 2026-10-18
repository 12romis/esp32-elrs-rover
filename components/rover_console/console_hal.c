// HAL консолі: читає байти з USB-Serial-JTAG, збирає рядок через console_core, виконує команди.
#include <stdio.h>

#include "app_state_hal.h"
#include "console.h"
#include "console_core.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CONSOLE_STACK 4096
#define CONSOLE_PRIORITY 1
#define CONSOLE_CORE 0
#define REBOOT_FLUSH_MS 200   // скільки чекати спорожнення USB-буфера перед рестартом

static const char *TAG = "console";

static const console_ops_t *s_ops;

static void say(const char *text)
{
    printf("%s\r\n", text);
    fflush(stdout);
}

static void do_status(void)
{
    uint64_t up_s = (uint64_t)(esp_timer_get_time() / 1000000);
    printf("стан: %s\r\n", app_state_name(app_state_get()));
    printf("час роботи: %llu с\r\n", (unsigned long long)up_s);
    printf("вільна внутрішня памʼять: %u байт\r\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    printf("причина останнього скидання: %s\r\n", s_ops->reset_reason ? s_ops->reset_reason() : "?");
    char storage[80] = "?";
    if (s_ops->storage_status) {
        s_ops->storage_status(storage, sizeof(storage));
    }
    printf("сховище: %s\r\n", storage);

    app_sm_entry_t hist[APP_SM_HISTORY_MAX];
    size_t n = app_state_history(hist, APP_SM_HISTORY_MAX);
    printf("історія переходів (%u):\r\n", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        printf("  %u мс: %s -> %s\r\n", (unsigned)hist[i].t_ms, app_state_name(hist[i].from),
               app_state_name(hist[i].to));
    }
    fflush(stdout);
}

static void execute(const char *line)
{
    console_cmd_t cmd = console_parse(line, app_state_get());
    switch (cmd) {
    case CONSOLE_CMD_EMPTY:
        break;
    case CONSOLE_CMD_UNKNOWN:
        say(console_msg_unknown());
        break;
    case CONSOLE_CMD_EXTRA_ARGS:
        say(console_msg_extra_args());
        break;
    case CONSOLE_CMD_DENIED_STATE:
        say(console_msg_denied(console_identify(line)));
        break;
    case CONSOLE_CMD_HELP:
        say(console_help_text());
        break;
    case CONSOLE_CMD_STATUS:
        do_status();
        break;
    case CONSOLE_CMD_REBOOT:
        say("Перезавантаження плати...");
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(REBOOT_FLUSH_MS));  // щоб повідомлення встигло вийти
        if (s_ops->reboot) {
            s_ops->reboot();
        } else {
            esp_restart();
        }
        break;
    case CONSOLE_CMD_WDT_TEST:
        say("Навмисне зависання службової задачі: плата перезавантажиться приблизно за 5 с "
            "з причиною TASK_WDT.");
        if (s_ops->request_hang) {
            s_ops->request_hang();
        }
        break;
    }
}

static void console_task(void *arg)
{
    (void)arg;
    console_t con;
    console_init(&con);
    for (;;) {
        char ch;
        if (usb_serial_jtag_read_bytes(&ch, 1, portMAX_DELAY) != 1) {
            continue;
        }
        console_event_t ev = console_feed(&con, ch);
        // Ехо: дзеркалимо введене, бо монітор не друкує ввід сам.
        if (ev != CONSOLE_EVENT_NONE) {
            printf("\r\n");
        } else if (ch == 0x08 || ch == 0x7F) {
            printf("\b \b");
        } else if ((unsigned char)ch >= 0x20 || ch == '\t') {
            putchar(ch);
        }
        fflush(stdout);
        if (ev == CONSOLE_EVENT_LINE) {
            execute(console_line(&con));
        } else if (ev == CONSOLE_EVENT_OVERFLOW) {
            say(console_msg_overflow());
        }
    }
}

esp_err_t console_start(const console_ops_t *ops)
{
    if (ops == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_ops = ops;

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
        return err;
    }
    // Вивід (printf, ESP_LOG) теж через драйвер, як у еталонному REPL ESP-IDF.
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreatePinnedToCore(console_task, "console", CONSOLE_STACK, NULL, CONSOLE_PRIORITY, NULL,
                                CONSOLE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу console");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "консоль готова, наберіть help");
    return ESP_OK;
}
