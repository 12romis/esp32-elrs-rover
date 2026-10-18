// HAL приймача CRSF: UART2, задача crsf_rx, публікація знімка (ADR-0001). Логіка розбору — в crsf_core.c.
#include "crsf_hal.h"

#include <string.h>

#include "board_pins.h"
#include "crsf_calib.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "crsf";

#define CRSF_UART UART_NUM_2
#define CRSF_RX_BUF 512        // більше за апаратний FIFO (UART_HW_FIFO_LEN), інакше драйвер не встановиться
#define CRSF_EVT_QUEUE 16
#define CRSF_STACK 3072
#define CRSF_PRIORITY 6
#define CRSF_CORE 1
#define CRSF_WAKE_MS 100       // прокидання без подій, щоб скидати Task WDT і за вимкненого пульта
#define CRSF_READ_CHUNK 64

static crsf_parser_t s_parser;            // лише задача crsf_rx
static crsf_snapshot_t s_snap;            // публікується під s_mux
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_queue;
static bool s_running;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void publish(void)
{
    portENTER_CRITICAL(&s_mux);
    s_snap = s_parser.snap;
    portEXIT_CRITICAL(&s_mux);
}

void crsf_snapshot_get(crsf_snapshot_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_snap;
    portEXIT_CRITICAL(&s_mux);
}

bool crsf_hal_running(void)
{
    return s_running;
}

// Читає все, що накопичив драйвер, порціями; мітка часу — момент читання (spec §6: мітку ставить crsf_rx).
static void drain(void)
{
    uint8_t buf[CRSF_READ_CHUNK];
    for (;;) {
        int n = uart_read_bytes(CRSF_UART, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        crsf_feed(&s_parser, buf, (size_t)n, now_ms());
    }
}

static void crsf_rx_task(void *arg)
{
    (void)arg;
    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK) {
        // Приймач без Task WDT працює, але це видно в журналі; безпеку руху тримає control.
        ESP_LOGE(TAG, "esp_task_wdt_add: %s — crsf_rx працює без сторожового таймера", esp_err_to_name(err));
    }
    uart_event_t evt;
    for (;;) {
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(CRSF_WAKE_MS)) == pdTRUE) {
            switch (evt.type) {
            case UART_DATA:
                drain();
                break;
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                // Переповнення: частину байтів втрачено, вхідний буфер скидаємо, подію рахуємо (таймер зв'язку
                // реагує на це так само, як на тишу).
                uart_flush_input(CRSF_UART);
                xQueueReset(s_queue);
                crsf_note_overflow(&s_parser);
                break;
            default:
                break;   // BREAK, PARITY_ERR, FRAME_ERR тощо: биті байти відкине збирач за CRC
            }
            publish();
        }
        if (err == ESP_OK) {
            esp_task_wdt_reset();
        }
    }
}

esp_err_t crsf_start(void)
{
    crsf_parser_init(&s_parser);
    memset(&s_snap, 0, sizeof(s_snap));

    const uart_config_t cfg = {
        .baud_rate = CRSF_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(CRSF_UART, CRSF_RX_BUF, 0, CRSF_EVT_QUEUE, &s_queue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s — приймання недоступне, ровер у стендовому режимі", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(CRSF_UART, &cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(CRSF_UART, PIN_ELRS_TX, PIN_ELRS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "налаштування UART: %s — приймання недоступне, ровер у стендовому режимі", esp_err_to_name(err));
        uart_driver_delete(CRSF_UART);
        return err;
    }
    if (xTaskCreatePinnedToCore(crsf_rx_task, "crsf_rx", CRSF_STACK, NULL, CRSF_PRIORITY, NULL, CRSF_CORE) != pdPASS) {
        ESP_LOGE(TAG, "не вдалося створити задачу crsf_rx — ровер у стендовому режимі");
        uart_driver_delete(CRSF_UART);
        return ESP_ERR_NO_MEM;
    }
    s_running = true;
    ESP_LOGI(TAG, "приймач CRSF: UART2, %d бод 8N1", CRSF_BAUD);
    return ESP_OK;
}
