// P1 — скан I²C. Приймання: знайдено рівно одну адресу в 0x20…0x27 (PCF8574_ADDR).
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "board_pins.h"
#include "bringup.h"

static const char *TAG = "bringup-p1";

// P1: скан I²C, 100 кГц. Далекоміри від'єднані, на шині лише PCF8574.
// Підтяжки дає модуль PCF8574 (R17/R19 не встановлені), внутрішні вимкнено.
void test_p1_i2c_scan(void)
{
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

    int in_range = 0, total = 0;
    uint8_t found_in_range = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            bool pcf = addr >= 0x20 && addr <= 0x27;
            ESP_LOGI(TAG, "P1 found 0x%02X%s", addr, pcf ? "  (діапазон PCF8574)" : "");
            total++;
            if (pcf) {
                in_range++;
                found_in_range = addr;
            }
        }
    }
    ESP_LOGI(TAG, "P1 total on bus: %d", total);
    if (in_range == 1) {
        ESP_LOGI(TAG, "P1 PASS: PCF8574_ADDR = 0x%02X", found_in_range);
    } else {
        ESP_LOGE(TAG, "P1 FAIL: у 0x20..0x27 знайдено %d пристроїв (треба рівно 1)", in_range);
    }
    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
}
