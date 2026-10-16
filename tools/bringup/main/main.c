// Діагностичні прошивки bring-up (docs/02-FIRMWARE-CONTEXT.md §7.0).
// Результат кожної передфази записується в docs/measured.md.
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "board_pins.h"

#define BRINGUP_TEST 2   // 0=chip/psram 1=i2c scan 2=pcf 3=adc
                         // 4=tof 5=crsf 6=encoders 7=motors

static const char *TAG = "bringup";

// P0: тулчейн, чіп, флеш, PSRAM
static __attribute__((unused)) void test_p0_chip_psram(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "P0 chip: %s rev %d.%d, cores %d, IDF %s",
             CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100,
             chip.cores, esp_get_idf_version());

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "P0 flash: %u MB", (unsigned)(flash_size / (1024 * 1024)));
    ESP_LOGI(TAG, "P0 psram: %u bytes", (unsigned)esp_psram_get_size());
    ESP_LOGI(TAG, "P0 free heap: %u (internal %u)",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// P1: скан I²C, 100 кГц. Далекоміри від'єднані, на шині лише PCF8574.
// Підтяжки дає модуль PCF8574 (R17/R19 не встановлені), внутрішні вимкнено.
static __attribute__((unused)) void test_p1_i2c_scan(void)
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

// P2: виходи PCF8574 і світло. Логіка не інвертована: 1 = горить.
// Перша операція на шині — запис 0x00 (гасить усе й тримає XSHUT у нулі).
static __attribute__((unused)) void test_p2_pcf_outputs(void)
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

void app_main(void)
{
#if BRINGUP_TEST == 0
    test_p0_chip_psram();
#elif BRINGUP_TEST == 1
    test_p1_i2c_scan();
#elif BRINGUP_TEST == 2
    test_p2_pcf_outputs();
#else
#error "Тест ще не реалізований"
#endif
}
