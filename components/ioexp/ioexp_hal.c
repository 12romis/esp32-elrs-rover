// HAL розширювача портів (ADR-0003): нуль першим, доступність — підтверджений запис, скан лише журналиться.
#include "ioexp_hal.h"

#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus_core.h"
#include "i2c_bus_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define WRITE_TIMEOUT_MS 20     // таймаут запису байта (SAD §4)
#define PROBE_TIMEOUT_MS 10     // таймаут зонда скану
#define START_ATTEMPTS 2        // до двох спроб запису нуля
#define LOCK_TIMEOUT_MS 50
#define MUTEX_TIMEOUT_MS 50     // чекання внутрішнього мʼютекса байта (ui і sensors пишуть у розширювач, ADR-0002 phase6-tof)
#define REFRESH_MS 1000         // періодичний перезапис байта: розширювач міг втратити живлення й піднятися з усіма виходами
#define RESET_EVERY_FAILS 5     // після такої кількості невдач підряд скинути стан шини

static const char *TAG = "ioexp";

static ioexp_t s_e;
static SemaphoreHandle_t s_mutex;   // один запис байта за раз: ui (світло) і sensors (лінії скидання)
static i2c_master_dev_handle_t s_dev;
static uint32_t s_last_ok_ms;
static int s_fail_streak;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool write_byte(uint8_t value)
{
    if (s_dev == NULL || !i2c_bus_lock(LOCK_TIMEOUT_MS)) {
        return false;
    }
    esp_err_t err = i2c_master_transmit(s_dev, &value, 1, WRITE_TIMEOUT_MS);
    i2c_bus_unlock();
    return err == ESP_OK;
}

esp_err_t ioexp_start(void)
{
    ioexp_init(&s_e);
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    esp_err_t err = i2c_bus_init();
    if (err == ESP_OK) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PCF8574_ADDR,  // виміряна адреса (docs/measured.md), не результат скану
            .scl_speed_hz = 100000,
        };
        err = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_bus_add_device: %s", esp_err_to_name(err));
            s_dev = NULL;
        }
    }
    bool ack = false;
    if (err == ESP_OK) {
        for (int attempt = 0; attempt < START_ATTEMPTS && !ack; attempt++) {
            ack = write_byte(0x00);  // перша операція на шині після старту (конституція, п. 7)
        }
    }
    ioexp_start_result(&s_e, ack);
    uint32_t t = now_ms();
    if (!ack) {
        ESP_LOGW(TAG, "розширювач не відповів на запис нуля (мітка %u мс) — світло недоступне до перезапуску плати, скан пропущено; "
                 "мотори, консоль і зумер працюють", (unsigned)t);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    s_last_ok_ms = t;
    ESP_LOGI(TAG, "перший запис нуля підтверджено, мітка %u мс — світло вимкнене", (unsigned)t);

    i2c_scan_t scan;
    i2c_scan_init(&scan);
    for (int addr = I2C_BUS_ADDR_MIN; addr <= I2C_BUS_ADDR_MAX; addr++) {
        if (i2c_bus_probe((uint8_t)addr, PROBE_TIMEOUT_MS) == ESP_OK) {
            i2c_scan_add(&scan, (uint8_t)addr);
        }
    }
    char line[96];
    i2c_scan_format(line, sizeof(line), &scan, PCF8574_ADDR);
    ESP_LOGI(TAG, "скан шини: %s", line);
    return ESP_OK;
}

bool ioexp_hal_available(void)
{
    return ioexp_available(&s_e);
}

bool ioexp_hal_bit(int bit)
{
    return ioexp_available(&s_e) && ioexp_bit(&s_e, bit);
}

static ioexp_wr_t apply_locked(uint8_t desired_light_bits);

// Спільна обробка результату запису байта для обох шляхів (світло і лінії скидання, review R5): лічильник невдач, скидання
// шини, повідомлення про збій і відновлення, рівень журналу драйвера шини. Викликається під внутрішнім мʼютексом.
static void after_write(bool ack, ioexp_event_t ev)
{
    if (ack) {
        s_last_ok_ms = now_ms();
        s_fail_streak = 0;
    } else if (++s_fail_streak % RESET_EVERY_FAILS == 0) {
        // шина могла «зависнути» після втрати живлення розширювача: скидаємо її стан і пробуємо далі
        esp_err_t rst = i2c_bus_reset();
        if (s_fail_streak == RESET_EVERY_FAILS || s_fail_streak % 600 == 0) {  // не частіше разу на хвилину
            ESP_LOGW(TAG, "%d невдач підряд — скидання шини: %s", s_fail_streak, esp_err_to_name(rst));
        }
    }
    if (ev == IOEXP_EVT_FAULT) {
        esp_log_level_set("i2c.master", ESP_LOG_NONE);  // драйвер пише рядок на кожну невдалу транзакцію: ~10 на секунду, доки шина мертва
        ESP_LOGW(TAG, "шина не підтвердила запис — світло і лінії скидання лишаються в останньому підтвердженому стані, автоматика повторює");
    } else if (ev == IOEXP_EVT_RECOVERED) {
        esp_log_level_set("i2c.master", ESP_LOG_ERROR);
        ESP_LOGI(TAG, "шина відновилась — світло й лінії скидання приведено до бажаного стану");
    }
}

ioexp_wr_t ioexp_hal_apply(uint8_t desired_light_bits)
{
    if (!ioexp_available(&s_e)) {
        return IOEXP_WR_UNAVAILABLE;
    }
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return IOEXP_WR_FAILED;   // копія не змінена, автоматика повторить
    }
    ioexp_wr_t r = apply_locked(desired_light_bits);
    xSemaphoreGive(s_mutex);
    return r;
}

ioexp_wr_t ioexp_hal_xshut(int bit, bool high)
{
    if (!ioexp_available(&s_e)) {
        return IOEXP_WR_UNAVAILABLE;
    }
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return IOEXP_WR_FAILED;
    }
    ioexp_wr_t r = IOEXP_WR_FAILED;
    if (ioexp_xshut_set(&s_e, bit, high)) {
        // світло лишаємо таким, яким воно підтверджене: бажане світло ui застосує сам наступним apply
        uint8_t byte;
        if (ioexp_prepare(&s_e, (uint8_t)(s_e.shadow & IOEXP_LIGHT_MASK), &byte)) {
            bool ack = write_byte(byte);
            ioexp_event_t ev = ioexp_commit(&s_e, byte, ack);
            after_write(ack, ev);
            if (ack) {
                r = IOEXP_WR_OK;
            } else {
                ioexp_xshut_revert(&s_e);   // розряд лишається таким, яким підтверджений
            }
        }
    }
    xSemaphoreGive(s_mutex);
    return r;
}

static ioexp_wr_t apply_locked(uint8_t desired_light_bits)
{
    bool refresh = (uint32_t)(now_ms() - s_last_ok_ms) >= REFRESH_MS;
    if (!ioexp_needs_write(&s_e, desired_light_bits) && !refresh) {
        return IOEXP_WR_OK;
    }
    uint8_t byte;
    if (!ioexp_prepare(&s_e, desired_light_bits, &byte)) {
        return IOEXP_WR_UNAVAILABLE;
    }
    bool ack = write_byte(byte);
    ioexp_event_t ev = ioexp_commit(&s_e, byte, ack);
    after_write(ack, ev);
    return ack ? IOEXP_WR_OK : IOEXP_WR_FAILED;
}
