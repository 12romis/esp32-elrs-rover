// Єдине місце з номерами GPIO. Джерело: kicad-rover.net, docs/02-FIRMWARE-CONTEXT.md §2.
// Це фізичні доріжки — не міняти без перерозводки.
#pragma once

// Батарея (ADC1_CH0)
#define PIN_VBAT_SENSE   1

// I²C (PCF8574, VL53L0X)
#define PIN_I2C_SCL      8
#define PIN_I2C_SDA      9

// Енкодери (PCNT units 0..3)
#define PIN_ENC_FL_A     4
#define PIN_ENC_FL_B     5
#define PIN_ENC_FR_A     6
#define PIN_ENC_FR_B     7
#define PIN_ENC_RL_A     10
#define PIN_ENC_RL_B     11
#define PIN_ENC_RR_A     12
#define PIN_ENC_RR_B     13

// Мотори (TB6612FNG ×2)
#define PIN_PWM_L        15
#define PIN_PWM_R        18
#define PIN_DIR_L1       38
#define PIN_DIR_L2       39
#define PIN_DIR_R1       40
#define PIN_DIR_R2       41
#define PIN_STBY         21   // апаратний failsafe, 0 = мотори вимкнені

// Зумер (LEDC)
#define PIN_BUZZER       14

// ELRS / CRSF (UART2)
#define PIN_ELRS_TX      16
#define PIN_ELRS_RX      17

// Резерв J8
#define PIN_J8_GPIO2     2
#define PIN_J8_GPIO42    42
#define PIN_J8_GPIO43    43
#define PIN_J8_GPIO44    44

// Біти PCF8574 (U6)
#define PCF8574_ADDR           0x20   // виміряно P1: 2026-10-16 (docs/measured.md)
#define IOEXP_BIT_XSHUT_U7     0
#define IOEXP_BIT_XSHUT_U8     1
#define IOEXP_BIT_XSHUT_U9     2
#define IOEXP_BIT_LIGHT_EN     3   // 1 = фари горять
#define IOEXP_BIT_XSHUT_U10    4   // не P3!
#define IOEXP_BIT_STOP_RED     5   // 1 = горить
#define IOEXP_BIT_STOP_GREEN   6   // 1 = горить
// P7 вільний
