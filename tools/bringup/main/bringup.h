// Діагностичні прошивки bring-up P0–P7 (docs/02-FIRMWARE-CONTEXT.md §7.0).
#pragma once

void test_p0_chip_psram(void);
void test_p1_i2c_scan(void);
void test_p2_pcf_outputs(void);
void test_p3_adc_battery(void);
void test_p4_tof(void);
void test_p5_crsf(void);
void test_p6_encoders(void);
void test_p7_motors(void);

// Спільне для P6 і P7: 4 юніти PCNT у квадратурному режимі.
// Індекси: 0=FL 1=FR 2=RL 3=RR.
void enc_init(int use_pullup);
int enc_count(int idx);
extern const char *const ENC_NAMES[4];
