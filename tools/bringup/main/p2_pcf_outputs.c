// P2 — виходи PCF8574 і світло. Приймання: після 0x00 світла немає, кожен канал незалежний.
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

static const char *TAG = "bringup-p2";

// P2: виходи PCF8574 і світло. Логіка не інвертована: 1 = горить.
// Перша операція на шині — запис 0x00 (гасить усе й тримає XSHUT у нулі).
void test_p2_pcf_outputs(void)
{
    static const struct { uint8_t bit; const char *name; } ch[] = {
        { IOEXP_BIT_LIGHT_EN,   "P3 LIGHT_EN (фари J2/J3)" },
        { IOEXP_BIT_STOP_RED,   "P5 STOP_RED (червоні J5/J6)" },
        { IOEXP_BIT_STOP_GREEN, "P6 STOP_GREEN (зелені J5/J6)" },
    };

    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8574_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t pcf;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &pcf));

    uint8_t v = 0x00;
    ESP_ERROR_CHECK(i2c_master_transmit(pcf, &v, 1, 100));
    ESP_LOGI(TAG, "P2 0x00 записано: усе світло має бути вимкнене");
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (;;) {
        for (size_t i = 0; i < sizeof(ch) / sizeof(ch[0]); i++) {
            v = 1u << ch[i].bit;
            ESP_ERROR_CHECK(i2c_master_transmit(pcf, &v, 1, 100));
            ESP_LOGI(TAG, "P2 ON  0x%02X  %s", v, ch[i].name);
            vTaskDelay(pdMS_TO_TICKS(1000));
            v = 0x00;
            ESP_ERROR_CHECK(i2c_master_transmit(pcf, &v, 1, 100));
            ESP_LOGI(TAG, "P2 OFF 0x00");
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        v = (1u << IOEXP_BIT_LIGHT_EN) | (1u << IOEXP_BIT_STOP_RED) | (1u << IOEXP_BIT_STOP_GREEN);
        ESP_ERROR_CHECK(i2c_master_transmit(pcf, &v, 1, 100));
        ESP_LOGI(TAG, "P2 ON  0x%02X  усі три разом", v);
        vTaskDelay(pdMS_TO_TICKS(1000));
        v = 0x00;
        ESP_ERROR_CHECK(i2c_master_transmit(pcf, &v, 1, 100));
        ESP_LOGI(TAG, "P2 OFF 0x00 (пауза 2 с)");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
