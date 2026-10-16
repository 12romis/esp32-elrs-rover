// P5 — приймання CRSF. Приймач у J7, привʼязаний до TX12.
// Визначає CRSF_BAUD (конфлікт 10.2) і справжню карту каналів (§1.3).
// Приймання: CRC fail < 1 %; кожен орган керування монотонно змінює рівно один канал 172…1811;
// при вимкненні пульта кадри припиняються.
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

#define P5_BAUD 420000   // якщо приймач мовчить, спробувати 416666

#define CRSF_SYNC          0xC8
#define CRSF_TYPE_RC       0x16
#define CRSF_MAX_LEN       62     // type + payload + crc
#define CRSF_RC_PAYLOAD    22

static const char *TAG = "bringup-p5";

static uint8_t crc8_d5(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// 16 каналів по 11 біт, LSB-first
static void unpack_channels(const uint8_t *p, uint16_t ch[16])
{
    uint32_t acc = 0;
    int bits = 0, n = 0;
    for (int i = 0; i < CRSF_RC_PAYLOAD && n < 16; i++) {
        acc |= (uint32_t)p[i] << bits;
        bits += 8;
        while (bits >= 11 && n < 16) {
            ch[n++] = acc & 0x7FF;
            acc >>= 11;
            bits -= 11;
        }
    }
}

static int to_us(uint16_t v)
{
    return ((int)v - 992) * 5 / 8 + 1500;   // 172→988, 992→1500, 1811→2012
}

void test_p5_crsf(void)
{
    const uart_config_t cfg = {
        .baud_rate = P5_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, PIN_ELRS_TX, PIN_ELRS_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "P5 UART2 %d 8N1, RX=GPIO%d TX=GPIO%d", P5_BAUD, PIN_ELRS_RX, PIN_ELRS_TX);

    uint8_t buf[128];
    int n = 0;
    uint32_t ok = 0, fail = 0, rc_frames = 0;
    uint8_t last_type = 0;
    uint16_t ch[16] = { 0 };
    int64_t last_ok_us = 0, last_print_us = 0;

    for (;;) {
        uint8_t tmp[64];
        int got = uart_read_bytes(UART_NUM_2, tmp, sizeof(tmp), pdMS_TO_TICKS(20));
        for (int i = 0; i < got; i++) {
            if (n < (int)sizeof(buf)) {
                buf[n++] = tmp[i];
            }
        }

        // Збирання кадрів [sync][len][type][payload][crc]. Битий кадр відкидається,
        // пошук продовжується з наступного байта (CRC fail тут може бути завищений
        // на повторній синхронізації, це відомо).
        while (n >= 2) {
            if (buf[0] != CRSF_SYNC) {
                memmove(buf, buf + 1, --n);
                continue;
            }
            int len = buf[1];
            if (len < 2 || len > CRSF_MAX_LEN) {
                memmove(buf, buf + 1, --n);
                continue;
            }
            int total = len + 2;
            if (n < total) {
                break;
            }
            if (crc8_d5(buf + 2, len - 1) == buf[total - 1]) {
                ok++;
                last_ok_us = esp_timer_get_time();
                last_type = buf[2];
                if (buf[2] == CRSF_TYPE_RC && len - 2 == CRSF_RC_PAYLOAD) {
                    unpack_channels(buf + 3, ch);
                    rc_frames++;
                }
                memmove(buf, buf + total, n - total);
                n -= total;
            } else {
                fail++;
                memmove(buf, buf + 1, --n);
            }
        }

        int64_t now = esp_timer_get_time();
        if (now - last_print_us >= 500000) {
            last_print_us = now;
            uint32_t total_frames = ok + fail;
            ESP_LOGI(TAG, "P5 crc ok=%u fail=%u (%.2f%%) rc=%u lasttype=0x%02X silent=%lld ms",
                     (unsigned)ok, (unsigned)fail,
                     total_frames ? 100.0 * fail / total_frames : 0.0,
                     (unsigned)rc_frames, last_type,
                     last_ok_us ? (long long)((now - last_ok_us) / 1000) : -1);
            if (rc_frames) {
                ESP_LOGI(TAG, "P5 raw  %4u %4u %4u %4u %4u %4u %4u %4u | %4u %4u %4u %4u %4u %4u %4u %4u",
                         ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7],
                         ch[8], ch[9], ch[10], ch[11], ch[12], ch[13], ch[14], ch[15]);
                ESP_LOGI(TAG, "P5 us   %4d %4d %4d %4d %4d %4d %4d %4d | %4d %4d %4d %4d %4d %4d %4d %4d",
                         to_us(ch[0]), to_us(ch[1]), to_us(ch[2]), to_us(ch[3]),
                         to_us(ch[4]), to_us(ch[5]), to_us(ch[6]), to_us(ch[7]),
                         to_us(ch[8]), to_us(ch[9]), to_us(ch[10]), to_us(ch[11]),
                         to_us(ch[12]), to_us(ch[13]), to_us(ch[14]), to_us(ch[15]));
            }
        }
    }
}
