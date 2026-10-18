// Хостові тести i2c_bus_core (spec AC-01, AC-02): оцінка результату скану й рядок журналу.
#include <string.h>

#include "i2c_bus_core.h"
#include "test_support.h"

static void test_empty(void)
{
    i2c_scan_t s;
    i2c_scan_init(&s);
    CHECK(s.count == 0);
    CHECK(!i2c_scan_contains(&s, 0x20));
    CHECK(i2c_scan_others(&s, 0x20) == 0);
    char buf[96];
    i2c_scan_format(buf, sizeof buf, &s, 0x20);
    CHECK(strstr(buf, "на шині нічого не знайдено") != NULL);
    CHECK(strstr(buf, "очікувана 0x20 не знайдена") != NULL);
}

static void test_only_expected(void)
{
    i2c_scan_t s;
    i2c_scan_init(&s);
    CHECK(i2c_scan_add(&s, 0x20));
    CHECK(s.count == 1);
    CHECK(i2c_scan_contains(&s, 0x20));
    CHECK(i2c_scan_others(&s, 0x20) == 0);
    char buf[96];
    i2c_scan_format(buf, sizeof buf, &s, 0x20);
    CHECK(strstr(buf, "0x20") != NULL);
    CHECK(strstr(buf, "знайдено 1: 0x20") != NULL);
    CHECK(strstr(buf, "очікувана 0x20 є") != NULL);
    CHECK(strstr(buf, "не знайден") == NULL);
}

static void test_others_are_info(void)
{
    i2c_scan_t s;
    i2c_scan_init(&s);
    i2c_scan_add(&s, 0x20);
    i2c_scan_add(&s, 0x30);
    i2c_scan_add(&s, 0x31);
    CHECK(i2c_scan_others(&s, 0x20) == 2);
    CHECK(i2c_scan_contains(&s, 0x30));
    char buf[96];
    i2c_scan_format(buf, sizeof buf, &s, 0x20);
    CHECK(strstr(buf, "0x30") != NULL && strstr(buf, "0x31") != NULL);
    CHECK(strstr(buf, "знайдено 3:") != NULL);
    CHECK(strstr(buf, "очікувана 0x20 є") != NULL);
}

static void test_only_others(void)
{
    i2c_scan_t s;
    i2c_scan_init(&s);
    i2c_scan_add(&s, 0x29);
    CHECK(!i2c_scan_contains(&s, 0x20));
    CHECK(i2c_scan_others(&s, 0x20) == 1);
    char buf[96];
    i2c_scan_format(buf, sizeof buf, &s, 0x20);
    CHECK(strstr(buf, "очікувана 0x20 не знайдена") != NULL);
}

static void test_range_and_duplicates(void)
{
    i2c_scan_t s;
    i2c_scan_init(&s);
    CHECK(!i2c_scan_add(&s, 0x07));      // нижче межі
    CHECK(!i2c_scan_add(&s, 0x78));      // вище межі
    CHECK(i2c_scan_add(&s, 0x08));       // межа входить
    CHECK(i2c_scan_add(&s, 0x77));       // межа входить
    CHECK(!i2c_scan_add(&s, 0x08));      // дубль
    CHECK(s.count == 2);
    // заповнення всього діапазону не переповнює масив
    i2c_scan_init(&s);
    for (int a = 0; a <= 0xFF; a++) i2c_scan_add(&s, (uint8_t)a);
    CHECK(s.count == I2C_SCAN_CAPACITY);
}

static void test_format_truncation(void)
{
    i2c_scan_t s;
    i2c_scan_init(&s);
    for (int a = I2C_BUS_ADDR_MIN; a <= I2C_BUS_ADDR_MAX; a++) i2c_scan_add(&s, (uint8_t)a);
    char small[16];
    i2c_scan_format(small, sizeof small, &s, 0x20);
    CHECK(strlen(small) < sizeof small);  // не виходить за буфер, завершено нулем
    i2c_scan_format(NULL, 0, &s, 0x20);   // не падає
    CHECK(1);
}

int main(void)
{
    test_empty();
    test_only_expected();
    test_others_are_info();
    test_only_others();
    test_range_and_duplicates();
    test_format_truncation();
    return test_summary();
}
