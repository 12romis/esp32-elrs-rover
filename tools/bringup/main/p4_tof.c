// P4 — далекоміри VL53L0X і перепризначення адрес (§4.7, варіант 3 із §10.3: мінімальний драйвер).
// Вимагає пройденої P1. Порядок (док. 02, §7.0):
//   1) P4_MODE 0: один далекомір (U7), видно 0x29, віддає відстань;
//   2) додавати решту по одному (міняти P4_SINGLE_BIT);
//   3) P4_MODE 1: повна послідовність XSHUT з перепризначенням на 0x30…0x33,
//      10 перезапусків поспіль, далі безперервне вимірювання чотирьох;
//   4) P4_MODE 2: те саме, але далі раз на секунду статистика по ~30 відліках
//      на сенсор (mean / min / max / скільки поза діапазоном) — для підбору
//      TOF_OBSTACLE_MM на зібраному шасі.
// Ініціалізаційні послідовності регістрів VL53L0X відтворюють відкрите ST/Pololu-ядро
// (spad info, tuning settings, ref calibration). Не перевірено на залізі.
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "bringup.h"

#define P4_MODE             2   // 0=один сенсор, 1=повна послідовність, 2=статистика
#define P4_SINGLE_BIT       IOEXP_BIT_XSHUT_U8   // U7=bit0, U8=bit1, U9=bit2, U10=bit4
#define P4_REPEAT           10

#define VL_DEFAULT_ADDR     0x29
#define REG_SYSRANGE_START              0x00
#define REG_SYSTEM_SEQUENCE_CONFIG      0x01
#define REG_SYSTEM_INTERRUPT_CONFIG     0x0A
#define REG_SYSTEM_INTERRUPT_CLEAR      0x0B
#define REG_RESULT_INTERRUPT_STATUS     0x13
#define REG_RESULT_RANGE_STATUS         0x14
#define REG_MSRC_CONFIG_CONTROL         0x60
#define REG_FINAL_RANGE_MIN_COUNT_RATE  0x44
#define REG_DYNAMIC_SPAD_NUM_REQUESTED  0x4E
#define REG_DYNAMIC_SPAD_START_OFFSET   0x4F
#define REG_GLOBAL_SPAD_ENABLES_REF_0   0xB0
#define REG_GLOBAL_REF_EN_START_SELECT  0xB6
#define REG_GPIO_HV_MUX_ACTIVE_HIGH     0x84
#define REG_VHV_CONFIG_PAD_SCL_SDA      0x89
#define REG_I2C_SLAVE_DEVICE_ADDRESS    0x8A
#define REG_IDENTIFICATION_MODEL_ID     0xC0
#define VL_MODEL_ID                     0xEE

static const char *TAG = "bringup-p4";

static const uint8_t XSHUT_BITS[4] = {
    IOEXP_BIT_XSHUT_U7, IOEXP_BIT_XSHUT_U8, IOEXP_BIT_XSHUT_U9, IOEXP_BIT_XSHUT_U10,
};
static const uint8_t NEW_ADDR[4] = { 0x30, 0x31, 0x32, 0x33 };

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_pcf;
static uint8_t s_pcf_shadow;   // тіньовий регістр PCF8574 (світло лишається 0)

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t addr;
    uint8_t stop_variable;
} vl53_t;

// ───────── PCF8574 ─────────

static void pcf_write(uint8_t v)
{
    s_pcf_shadow = v;
    ESP_ERROR_CHECK(i2c_master_transmit(s_pcf, &s_pcf_shadow, 1, 100));
}

// ───────── регістровий доступ VL53L0X ─────────

static bool wr8(vl53_t *d, uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_master_transmit(d->dev, b, 2, 50) == ESP_OK;
}

static bool rd8(vl53_t *d, uint8_t reg, uint8_t *v)
{
    return i2c_master_transmit_receive(d->dev, &reg, 1, v, 1, 50) == ESP_OK;
}

static bool wr16(vl53_t *d, uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_master_transmit(d->dev, b, 3, 50) == ESP_OK;
}

static bool rd16(vl53_t *d, uint8_t reg, uint16_t *v)
{
    uint8_t r[2];
    if (i2c_master_transmit_receive(d->dev, &reg, 1, r, 2, 50) != ESP_OK) {
        return false;
    }
    *v = ((uint16_t)r[0] << 8) | r[1];
    return true;
}

static bool wrN(vl53_t *d, uint8_t reg, const uint8_t *src, int n)
{
    uint8_t b[8];
    b[0] = reg;
    for (int i = 0; i < n; i++) {
        b[1 + i] = src[i];
    }
    return i2c_master_transmit(d->dev, b, n + 1, 50) == ESP_OK;
}

static bool rdN(vl53_t *d, uint8_t reg, uint8_t *dst, int n)
{
    return i2c_master_transmit_receive(d->dev, &reg, 1, dst, n, 50) == ESP_OK;
}

#define W(r, v)  do { if (!wr8(d, (r), (v))) return false; } while (0)

static bool wait_nonzero(vl53_t *d, uint8_t reg, uint8_t mask, int timeout_ms)
{
    int64_t end = esp_timer_get_time() + timeout_ms * 1000;
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
    uint8_t v;
    if (!rd8(d, REG_IDENTIFICATION_MODEL_ID, &v) || v != VL_MODEL_ID) {
        ESP_LOGW(TAG, "P4 0x%02X: model id 0x%02X (очікується 0x%02X)", d->addr, v, VL_MODEL_ID);
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
        ESP_LOGW(TAG, "P4 0x%02X: spad info timeout", d->addr);
        return false;
    }
    uint8_t ref_spad_map[6];
    if (!rdN(d, REG_GLOBAL_SPAD_ENABLES_REF_0, ref_spad_map, 6)) return false;
    W(0xFF, 0x01);
    W(REG_DYNAMIC_SPAD_START_OFFSET, 0x00);
    W(REG_DYNAMIC_SPAD_NUM_REQUESTED, 0x2C);
    W(0xFF, 0x00);
    W(REG_GLOBAL_REF_EN_START_SELECT, 0xB4);
    uint8_t first = is_aperture ? 12 : 0;
    uint8_t enabled = 0;
    for (int i = 0; i < 48; i++) {
        if (i < first || enabled == spad_count) {
            ref_spad_map[i / 8] &= ~(1 << (i % 8));
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
    if (!rd8(d, REG_GPIO_HV_MUX_ACTIVE_HIGH, &v)) return false;
    W(REG_GPIO_HV_MUX_ACTIVE_HIGH, v & ~0x10);
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

// повертає відстань у мм, -1 при таймауті
static int vl_read_mm(vl53_t *d)
{
    if (!wait_nonzero(d, REG_RESULT_INTERRUPT_STATUS, 0x07, 100)) {
        return -1;
    }
    uint16_t mm;
    if (!rd16(d, REG_RESULT_RANGE_STATUS + 10, &mm)) {
        return -1;
    }
    wr8(d, REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
    return mm;
}

// ───────── підключення пристроїв ─────────

static bool vl_attach(vl53_t *d, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    d->addr = addr;
    return i2c_master_bus_add_device(s_bus, &cfg, &d->dev) == ESP_OK;
}

static void vl_detach(vl53_t *d)
{
    if (d->dev) {
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

static void bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    i2c_device_config_t pcf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8574_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &pcf, &s_pcf));
    pcf_write(0x00);   // перша операція: усе вимкнено, всі XSHUT у нулі
}

// ───────── сценарії ─────────

static void scan_bus(void)
{
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(s_bus, a, 50) == ESP_OK) {
            ESP_LOGI(TAG, "P4 scan: 0x%02X", a);
        }
    }
}

static __attribute__((unused)) void mode_single(void)
{
    pcf_write(1u << P4_SINGLE_BIT);
    vTaskDelay(pdMS_TO_TICKS(10));
    scan_bus();

    vl53_t d = { 0 };
    if (!vl_attach(&d, VL_DEFAULT_ADDR) || !vl_init(&d) || !vl_start_continuous(&d)) {
        ESP_LOGE(TAG, "P4 FAIL: сенсор на 0x29 не ініціалізувався (XSHUT bit %d)", P4_SINGLE_BIT);
        return;
    }
    ESP_LOGI(TAG, "P4 сенсор на 0x29 готовий. Підноси долоню і стіну");
    for (;;) {
        ESP_LOGI(TAG, "P4 range = %d mm", vl_read_mm(&d));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static bool run_sequence(vl53_t s[4])
{
    pcf_write(0x00);
    vTaskDelay(pdMS_TO_TICKS(20));   // усі сенсори в reset
    uint8_t mask = 0;
    for (int i = 0; i < 4; i++) {
        mask |= 1u << XSHUT_BITS[i];
        pcf_write(mask);
        vTaskDelay(pdMS_TO_TICKS(5));   // ≥2 мс після підняття XSHUT
        if (!vl_attach(&s[i], VL_DEFAULT_ADDR) || !vl_init(&s[i])) {
            ESP_LOGW(TAG, "P4 сенсор #%d: init на 0x29 не вдався", i);
            return false;
        }
        if (!vl_set_address(&s[i], NEW_ADDR[i])) {
            ESP_LOGW(TAG, "P4 сенсор #%d: set address 0x%02X не вдався", i, NEW_ADDR[i]);
            return false;
        }
        uint8_t id;
        if (!rd8(&s[i], REG_IDENTIFICATION_MODEL_ID, &id) || id != VL_MODEL_ID) {
            ESP_LOGW(TAG, "P4 сенсор #%d: не відповідає на 0x%02X", i, NEW_ADDR[i]);
            return false;
        }
    }
    return true;
}

static __attribute__((unused)) void mode_full(void)
{
    vl53_t s[4] = { 0 };
    int passed = 0;
    for (int run = 1; run <= P4_REPEAT; run++) {
        for (int i = 0; i < 4; i++) {
            vl_detach(&s[i]);
        }
        bool ok = run_sequence(s);
        ESP_LOGI(TAG, "P4 запуск %d/%d: %s", run, P4_REPEAT, ok ? "OK" : "FAIL");
        if (ok) {
            passed++;
        }
    }
    ESP_LOGI(TAG, "P4 послідовність: %d/%d успішно (приймання: %d/%d)",
             passed, P4_REPEAT, P4_REPEAT, P4_REPEAT);
    scan_bus();

    if (!run_sequence(s)) {
        ESP_LOGE(TAG, "P4 FAIL: чотири сенсори не піднялися");
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (!vl_start_continuous(&s[i])) {
            ESP_LOGE(TAG, "P4 сенсор #%d: start continuous не вдався", i);
            return;
        }
    }
#if P4_MODE == 2
    static const char *const NAMES[4] = { "U7(0x30)", "U8(0x31)", "U9(0x32)", "U10(0x33)" };
    const int N = 30;
    for (;;) {
        long sum[4] = { 0 };
        int mn[4] = { 99999, 99999, 99999, 99999 }, mx[4] = { 0 }, valid[4] = { 0 };
        for (int k = 0; k < N; k++) {
            for (int i = 0; i < 4; i++) {
                int mm = vl_read_mm(&s[i]);
                if (mm > 0 && mm < 8190) {
                    sum[i] += mm;
                    valid[i]++;
                    if (mm < mn[i]) mn[i] = mm;
                    if (mm > mx[i]) mx[i] = mm;
                }
            }
        }
        for (int i = 0; i < 4; i++) {
            if (valid[i]) {
                ESP_LOGI(TAG, "P4 stat %s: mean=%ld min=%d max=%d valid=%d/%d",
                         NAMES[i], sum[i] / valid[i], mn[i], mx[i], valid[i], N);
            } else {
                ESP_LOGI(TAG, "P4 stat %s: поза діапазоном (0/%d)", NAMES[i], N);
            }
        }
    }
#else
    for (;;) {
        ESP_LOGI(TAG, "P4 U7(0x30)=%d U8(0x31)=%d U9(0x32)=%d U10(0x33)=%d mm",
                 vl_read_mm(&s[0]), vl_read_mm(&s[1]), vl_read_mm(&s[2]), vl_read_mm(&s[3]));
        vTaskDelay(pdMS_TO_TICKS(200));
    }
#endif
}

void test_p4_tof(void)
{
    bus_init();
#if P4_MODE == 0
    mode_single();
#else
    mode_full();
#endif
}
