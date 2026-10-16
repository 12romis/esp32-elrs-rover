// P0 — тулчейн, чіп, флеш, PSRAM. Приймання: ESP32-S3, флеш 16MB, PSRAM 8388608 байт.
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "board_pins.h"
#include "bringup.h"

static const char *TAG = "bringup-p0";

// P0: тулчейн, чіп, флеш, PSRAM
void test_p0_chip_psram(void)
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
