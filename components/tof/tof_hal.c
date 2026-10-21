// HAL далекомірів (phase6-tof, ADR-0002, ADR-0003). Мінімальний власний драйвер VL53L0X (рішення D4, документ 02 §10.3):
// порт діагностичного драйвера tools/bringup/main/p4_tof.c (ініціалізаційні послідовності відтворюють відкрите ST/Pololu-ядро).
// Кожна транзакція — під мʼютексом шини периферії; лінії скидання змінює лише ioexp (ADR-0002).
#include "tof_hal.h"

#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus_hal.h"
#include "ioexp_hal.h"

static const char *TAG = "tof";

#define VL_DEFAULT_ADDR 0x29
#define LOCK_TIMEOUT_MS 50
#define INIT_TIMEOUT_MS 50       // транзакція в ініціалізації
#define LOOP_TIMEOUT_MS 10       // транзакція в циклі опитування (T11): завислий обмін не гальмує задачу довше
#define XSHUT_PAUSE_US 5000      // після підняття лінії скидання: ≥ 2 мс (документ 02 §4.7); 5 мс — перевірена послідовність P4

#define REG_SYSRANGE_START 0x00
#define REG_SYSTEM_SEQUENCE_CONFIG 0x01
#define REG_SYSTEM_INTERRUPT_CONFIG 0x0A
#define REG_SYSTEM_INTERRUPT_CLEAR 0x0B
#define REG_RESULT_INTERRUPT_STATUS 0x13
#define REG_RESULT_RANGE_STATUS 0x14
#define REG_MSRC_CONFIG_CONTROL 0x60
#define REG_FINAL_RANGE_MIN_COUNT_RATE 0x44
#define REG_DYNAMIC_SPAD_NUM_REQUESTED 0x4E
#define REG_DYNAMIC_SPAD_START_OFFSET 0x4F
#define REG_GLOBAL_SPAD_ENABLES_REF_0 0xB0
#define REG_GLOBAL_REF_EN_START_SELECT 0xB6
#define REG_HV_MUX_ACTIVE_HIGH 0x84
#define REG_VHV_CONFIG_PAD_SCL_SDA 0x89
#define REG_I2C_SLAVE_DEVICE_ADDRESS 0x8A
#define REG_IDENTIFICATION_MODEL_ID 0xC0
#define VL_MODEL_ID 0xEE

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t addr;
    uint8_t stop_variable;
    int tmo_ms;
} vl53_t;

// Лінії скидання за tof_id_t (біти лише з board_pins.h) і нові адреси.
static const int XSHUT_BIT[TOF_SENSORS] = {IOEXP_BIT_XSHUT_U7, IOEXP_BIT_XSHUT_U8, IOEXP_BIT_XSHUT_U9, IOEXP_BIT_XSHUT_U10};
static const uint8_t NEW_ADDR[TOF_SENSORS] = {0x30, 0x31, 0x32, 0x33};
static const char *const PLACE[TOF_SENSORS] = {"U7 (ліворуч-вперед)", "U8 (вперед)", "U9 (праворуч-вперед)", "U10 (назад)"};

static vl53_t s_vl[TOF_SENSORS];

// ───────── регістровий доступ під мʼютексом шини ─────────

static bool wr_bytes(vl53_t *d, const uint8_t *b, size_t n)
{
    if (d->dev == NULL || !i2c_bus_lock(LOCK_TIMEOUT_MS)) {
        return false;
    }
    esp_err_t err = i2c_master_transmit(d->dev, b, n, d->tmo_ms);
    i2c_bus_unlock();
    return err == ESP_OK;
}

static bool rd_bytes(vl53_t *d, uint8_t reg, uint8_t *dst, size_t n)
{
    if (d->dev == NULL || !i2c_bus_lock(LOCK_TIMEOUT_MS)) {
        return false;
    }
    esp_err_t err = i2c_master_transmit_receive(d->dev, &reg, 1, dst, n, d->tmo_ms);
    i2c_bus_unlock();
    return err == ESP_OK;
}

static bool wr8(vl53_t *d, uint8_t reg, uint8_t v)
{
    uint8_t b[2] = {reg, v};
    return wr_bytes(d, b, 2);
}

static bool rd8(vl53_t *d, uint8_t reg, uint8_t *v)
{
    return rd_bytes(d, reg, v, 1);
}

static bool wr16(vl53_t *d, uint8_t reg, uint16_t v)
{
    uint8_t b[3] = {reg, (uint8_t)(v >> 8), (uint8_t)v};
    return wr_bytes(d, b, 3);
}

static bool rd16(vl53_t *d, uint8_t reg, uint16_t *v)
{
    uint8_t r[2];
    if (!rd_bytes(d, reg, r, 2)) {
        return false;
    }
    *v = (uint16_t)(((uint16_t)r[0] << 8) | r[1]);
    return true;
}

static bool wrN(vl53_t *d, uint8_t reg, const uint8_t *src, int n)
{
    uint8_t b[8];
    b[0] = reg;
    for (int i = 0; i < n; i++) {
        b[1 + i] = src[i];
    }
    return wr_bytes(d, b, (size_t)n + 1);
}

#define W(r, v)                    \
    do {                           \
        if (!wr8(d, (r), (v))) {   \
            return false;          \
        }                          \
    } while (0)

// Очікування готовності регістра в ініціалізації: пауза всередині одного вимірювання, а не період задачі (check_delays.sh).
static bool wait_nonzero(vl53_t *d, uint8_t reg, uint8_t mask, int timeout_ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    uint8_t v;
    while (esp_timer_get_time() < end) {
        if (rd8(d, reg, &v) && (v & mask)) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

// ───────── ініціалізація (відтворює Pololu / ST) ─────────

static bool get_spad_info(vl53_t *d, uint8_t *count, bool *is_aperture)
{
    uint8_t tmp;
    W(0x80, 0x01); W(0xFF, 0x01); W(0x00, 0x00);
    W(0xFF, 0x06);
    if (!rd8(d, 0x83, &tmp)) return false;
    W(0x83, tmp | 0x04);
    W(0xFF, 0x07); W(0x81, 0x01); W(0x80, 0x01);
    W(0x94, 0x6B); W(0x83, 0x00);
    if (!wait_nonzero(d, 0x83, 0xFF, 500)) return false;
    W(0x83, 0x01);
    if (!rd8(d, 0x92, &tmp)) return false;
    *count = tmp & 0x7F;
    *is_aperture = (tmp >> 7) & 1;
    W(0x81, 0x00); W(0xFF, 0x06);
    if (!rd8(d, 0x83, &tmp)) return false;
    W(0x83, tmp & ~0x04);
    W(0xFF, 0x01); W(0x00, 0x01); W(0xFF, 0x00); W(0x80, 0x00);
    return true;
}

static bool single_ref_calibration(vl53_t *d, uint8_t vhv_init_byte)
{
    W(REG_SYSRANGE_START, 0x01 | vhv_init_byte);
    if (!wait_nonzero(d, REG_RESULT_INTERRUPT_STATUS, 0x07, 500)) return false;
    W(REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    W(REG_SYSRANGE_START, 0x00);
    return true;
}

static bool vl_init(vl53_t *d)
{
    uint8_t v = 0;
    if (!rd8(d, REG_IDENTIFICATION_MODEL_ID, &v) || v != VL_MODEL_ID) {
        ESP_LOGW(TAG, "0x%02X: ідентифікатор моделі 0x%02X (очікується 0x%02X)", d->addr, v, VL_MODEL_ID);
        return false;
    }
    // режим вводу/виводу 2V8 (модулі з LDO 2.8 В)
    if (!rd8(d, REG_VHV_CONFIG_PAD_SCL_SDA, &v)) return false;
    W(REG_VHV_CONFIG_PAD_SCL_SDA, v | 0x01);
    // стандартний I²C режим, збереження stop_variable
    W(0x88, 0x00);
    W(0x80, 0x01); W(0xFF, 0x01); W(0x00, 0x00);
    if (!rd8(d, 0x91, &d->stop_variable)) return false;
    W(0x00, 0x01); W(0xFF, 0x00); W(0x80, 0x00);
    // вимкнути перевірки SIGNAL_RATE_MSRC і SIGNAL_RATE_PRE_RANGE
    if (!rd8(d, REG_MSRC_CONFIG_CONTROL, &v)) return false;
    W(REG_MSRC_CONFIG_CONTROL, v | 0x12);
    if (!wr16(d, REG_FINAL_RANGE_MIN_COUNT_RATE, 32)) return false;   // 0.25 MCPS
    W(REG_SYSTEM_SEQUENCE_CONFIG, 0xFF);
    // reference SPAD
    uint8_t spad_count;
    bool is_aperture;
    if (!get_spad_info(d, &spad_count, &is_aperture)) {
        ESP_LOGW(TAG, "0x%02X: таймаут spad info", d->addr);
        return false;
    }
    uint8_t ref_spad_map[6];
    if (!rd_bytes(d, REG_GLOBAL_SPAD_ENABLES_REF_0, ref_spad_map, 6)) return false;
    W(0xFF, 0x01);
    W(REG_DYNAMIC_SPAD_START_OFFSET, 0x00);
    W(REG_DYNAMIC_SPAD_NUM_REQUESTED, 0x2C);
    W(0xFF, 0x00);
    W(REG_GLOBAL_REF_EN_START_SELECT, 0xB4);
    uint8_t first = is_aperture ? 12 : 0;
    uint8_t enabled = 0;
    for (int i = 0; i < 48; i++) {
        if (i < first || enabled == spad_count) {
            ref_spad_map[i / 8] &= (uint8_t)~(1u << (i % 8));
        } else if ((ref_spad_map[i / 8] >> (i % 8)) & 1) {
            enabled++;
        }
    }
    if (!wrN(d, REG_GLOBAL_SPAD_ENABLES_REF_0, ref_spad_map, 6)) return false;
    // tuning settings
    W(0xFF, 0x01); W(0x00, 0x00);
    W(0xFF, 0x00); W(0x09, 0x00); W(0x10, 0x00); W(0x11, 0x00);
    W(0x24, 0x01); W(0x25, 0xFF); W(0x75, 0x00);
    W(0xFF, 0x01); W(0x4E, 0x2C); W(0x48, 0x00); W(0x30, 0x20);
    W(0xFF, 0x00); W(0x30, 0x09); W(0x54, 0x00); W(0x31, 0x04); W(0x32, 0x03);
    W(0x40, 0x83); W(0x46, 0x25); W(0x60, 0x00); W(0x27, 0x00); W(0x50, 0x06);
    W(0x51, 0x00); W(0x52, 0x96); W(0x56, 0x08); W(0x57, 0x30); W(0x61, 0x00);
    W(0x62, 0x00); W(0x64, 0x00); W(0x65, 0x00); W(0x66, 0xA0);
    W(0xFF, 0x01); W(0x22, 0x32); W(0x47, 0x14); W(0x49, 0xFF); W(0x4A, 0x00);
    W(0xFF, 0x00); W(0x7A, 0x0A); W(0x7B, 0x00); W(0x78, 0x21);
    W(0xFF, 0x01); W(0x23, 0x34); W(0x42, 0x00); W(0x44, 0xFF); W(0x45, 0x26);
    W(0x46, 0x05); W(0x40, 0x40); W(0x0E, 0x06); W(0x20, 0x1A); W(0x43, 0x40);
    W(0xFF, 0x00); W(0x34, 0x03); W(0x35, 0x44);
    W(0xFF, 0x01); W(0x31, 0x04); W(0x4B, 0x09); W(0x4C, 0x05); W(0x4D, 0x04);
    W(0xFF, 0x00); W(0x44, 0x00); W(0x45, 0x20); W(0x47, 0x08); W(0x48, 0x28);
    W(0x67, 0x00); W(0x70, 0x04); W(0x71, 0x01); W(0x72, 0xFE); W(0x76, 0x00);
    W(0x77, 0x00);
    W(0xFF, 0x01); W(0x0D, 0x01);
    W(0xFF, 0x00); W(0x80, 0x01); W(0x01, 0xF8);
    W(0xFF, 0x01); W(0x8E, 0x01); W(0x00, 0x01);
    W(0xFF, 0x00); W(0x80, 0x00);
    // переривання «нові дані готові»
    W(REG_SYSTEM_INTERRUPT_CONFIG, 0x04);
    if (!rd8(d, REG_HV_MUX_ACTIVE_HIGH, &v)) return false;
    W(REG_HV_MUX_ACTIVE_HIGH, v & ~0x10);
    W(REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    W(REG_SYSTEM_SEQUENCE_CONFIG, 0xE8);
    // калібрування VHV і фази
    W(REG_SYSTEM_SEQUENCE_CONFIG, 0x01);
    if (!single_ref_calibration(d, 0x40)) return false;
    W(REG_SYSTEM_SEQUENCE_CONFIG, 0x02);
    if (!single_ref_calibration(d, 0x00)) return false;
    W(REG_SYSTEM_SEQUENCE_CONFIG, 0xE8);
    return true;
}

static bool vl_start_continuous(vl53_t *d)
{
    W(0x80, 0x01); W(0xFF, 0x01); W(0x00, 0x00);
    W(0x91, d->stop_variable);
    W(0x00, 0x01); W(0xFF, 0x00); W(0x80, 0x00);
    W(REG_SYSRANGE_START, 0x02);   // back-to-back
    return true;
}

typedef enum { VL_POLL_ERR, VL_POLL_NOT_READY, VL_POLL_OK } vl_poll_t;

// Неблокувальне читання (T11): готового відліку немає — NOT_READY; відлік прочитано й переривання скинуто — OK.
// Значення ≥ 8190 мм — «поза діапазоном»; tof_core сам розрізняє.
__attribute__((unused)) static vl_poll_t vl_poll(vl53_t *d, uint16_t *mm)
{
    uint8_t st;
    if (!rd8(d, REG_RESULT_INTERRUPT_STATUS, &st)) {
        return VL_POLL_ERR;
    }
    if ((st & 0x07) == 0) {
        return VL_POLL_NOT_READY;
    }
    if (!rd16(d, REG_RESULT_RANGE_STATUS + 10, mm)) {
        return VL_POLL_ERR;
    }
    if (!wr8(d, REG_SYSTEM_INTERRUPT_CLEAR, 0x01)) {
        return VL_POLL_ERR;
    }
    return VL_POLL_OK;
}

// ───────── підключення пристроїв ─────────

static bool vl_attach(vl53_t *d, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,   // 100 кГц: підвищувати заборонено (конституція, п. 14)
    };
    d->addr = addr;
    return i2c_master_bus_add_device(i2c_bus_handle(), &cfg, &d->dev) == ESP_OK;
}

static void vl_detach(vl53_t *d)
{
    if (d->dev != NULL) {
        i2c_master_bus_rm_device(d->dev);
        d->dev = NULL;
    }
}

static bool vl_set_address(vl53_t *d, uint8_t new_addr)
{
    if (!wr8(d, REG_I2C_SLAVE_DEVICE_ADDRESS, new_addr & 0x7F)) {
        return false;
    }
    vl_detach(d);
    return vl_attach(d, new_addr);
}

// ───────── послідовність одного датчика (документ 02 §4.7) ─────────

// Підняти лінію скидання, пауза, перевірка моделі на 0x29, ініціалізація, нова адреса, безперервний режим. При невдачі
// лінію скидання опускаємо назад: напівпроініціалізований датчик на 0x29 зіткнувся б з наступним.
static bool bring_up(tof_id_t id)
{
    vl53_t *d = &s_vl[id];
    vl_detach(d);
    d->tmo_ms = INIT_TIMEOUT_MS;
    if (ioexp_hal_xshut(XSHUT_BIT[id], true) != IOEXP_WR_OK) {
        return false;
    }
    esp_rom_delay_us(XSHUT_PAUSE_US);
    bool ok = vl_attach(d, VL_DEFAULT_ADDR) && vl_init(d) && vl_set_address(d, NEW_ADDR[id]) && vl_start_continuous(d);
    if (!ok) {
        vl_detach(d);
        ioexp_hal_xshut(XSHUT_BIT[id], false);
        return false;
    }
    d->tmo_ms = LOOP_TIMEOUT_MS;
    return true;
}

esp_err_t tof_start(void)
{
    int ok = 0;
    if (!ioexp_hal_available()) {
        ESP_LOGW(TAG, "розширювач недоступний — лінії скидання далекомірів піднімати нікуди: усі чотири у відмові, "
                      "захист від зіткнень вимкнено");
        return ESP_OK;
    }
    for (int i = 0; i < TOF_SENSORS; i++) {
        if (bring_up((tof_id_t)i)) {
            ok++;
            ESP_LOGI(TAG, "далекомір %s: працює, адреса 0x%02X", PLACE[i], s_vl[i].addr);
        } else {
            ESP_LOGW(TAG, "далекомір %s: відмова при старті", PLACE[i]);
        }
    }
    if (ok == 0) {
        ESP_LOGW(TAG, "жоден далекомір не працює — захист від зіткнень вимкнено");
    }
    return ESP_OK;
}
