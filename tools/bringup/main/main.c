// Діагностичні прошивки bring-up (docs/02-FIRMWARE-CONTEXT.md §7.0).
// Результат кожної передфази записується в docs/measured.md.
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

#define BRINGUP_TEST 0   // 0=chip/psram 1=i2c scan 2=pcf 3=adc
                         // 4=tof 5=crsf 6=encoders 7=motors

static const char *TAG = "bringup";

// P0: тулчейн, чіп, флеш, PSRAM
static void test_p0_chip_psram(void)
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

void app_main(void)
{
#if BRINGUP_TEST == 0
    test_p0_chip_psram();
#else
#error "Тест ще не реалізований"
#endif
}
