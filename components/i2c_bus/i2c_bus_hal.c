// HAL шини периферії: i2c_master 100 кГц і мʼютекс m_i2c (ADR-0001). Лише нові драйвери IDF (конституція, п. 5).
#include "i2c_bus_hal.h"

#include "board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define I2C_BUS_PORT I2C_NUM_0
#define I2C_GLITCH_IGNORE 7

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_mutex;

esp_err_t i2c_bus_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_IGNORE,
        .flags.enable_internal_pullup = false,  // підтяжки дає модуль розширювача (документ 02, §4.6)
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }
    ESP_LOGI(TAG, "шина I2C готова (100 кГц)");
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_handle(void)
{
    return s_bus;
}

bool i2c_bus_lock(uint32_t timeout_ms)
{
    return s_mutex != NULL && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void i2c_bus_unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t i2c_bus_reset(void)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!i2c_bus_lock(50)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_bus_reset(s_bus);
    i2c_bus_unlock();
    return err;
}

esp_err_t i2c_bus_probe(uint8_t addr, int timeout_ms)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!i2c_bus_lock(50)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_probe(s_bus, addr, timeout_ms);
    i2c_bus_unlock();
    return err;
}
