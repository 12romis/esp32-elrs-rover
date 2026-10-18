// Діагностичні прошивки bring-up (docs/02-FIRMWARE-CONTEXT.md §7.0).
// Результат кожної передфази записується в docs/measured.md.
// Кожна передфаза — окремий файл pN_*.c; тут лише вибір.
#include "bringup.h"

#define BRINGUP_TEST 7   // 0=chip/psram 1=i2c scan 2=pcf 3=adc
                         // 4=tof 5=crsf 6=encoders 7=motors
                         // 8=indicator (OQ-3) 9=buzzer (OQ-2)

void app_main(void)
{
#if BRINGUP_TEST == 0
    test_p0_chip_psram();
#elif BRINGUP_TEST == 1
    test_p1_i2c_scan();
#elif BRINGUP_TEST == 2
    test_p2_pcf_outputs();
#elif BRINGUP_TEST == 3
    test_p3_adc_battery();
#elif BRINGUP_TEST == 4
    test_p4_tof();
#elif BRINGUP_TEST == 5
    test_p5_crsf();
#elif BRINGUP_TEST == 6
    test_p6_encoders();
#elif BRINGUP_TEST == 7
    test_p7_motors();
#elif BRINGUP_TEST == 8
    test_p8_indicator();
#elif BRINGUP_TEST == 9
    test_p9_buzzer();
#else
#error "Тест ще не реалізований"
#endif
}
