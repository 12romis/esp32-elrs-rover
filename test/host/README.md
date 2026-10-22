# Хостові тести чистої логіки

Одна процедура запуску з кореня репозиторію:

```bash
test/host/run_tests.sh
```

Раннер збирає кожен `test_*.c` разом з усіма `components/*/*_core.c` звичайним gcc
(`-std=c11 -Wall -Wextra -Werror`), запускає і друкує `N тестів, M пройдено`. Нуль тестів — помилка.
Тест використовує `test_support.h` (макрос `CHECK`, функція `test_summary`).

Перевірки правил (кожна друкує файл і причину та повертає 1 при порушенні):

| Скрипт | Правило |
|---|---|
| `check_core_headers.sh` | `*_core.c` без заголовків ESP-IDF і FreeRTOS (AC-09, constitution п. 6) |
| `check_pins.sh` | номери GPIO лише в `board_pins.h` (AC-11, constitution п. 2) |
| `check_bus_rules.sh` | розряди розширювача лише з `board_pins.h`, без застарілих драйверів шини, шина ≤ 100 кГц (AC-25 phase2-io, constitution пп. 2, 5, 14) |
| `check_protected.sh` | захищені файли не змінені без згоди (AC-10, constitution п. 10–11); `ALLOW_PROTECTED=1` дозволяє лише `CMakeLists.txt` і `sdkconfig.defaults` (phase8) і друкує їх список, `.vscode/` та `kicad-rover/` — ніколи |
| `check_secrets.sh` | `report_secrets.h` у `.gitignore` і не в git, шаблон без значень, значення секретів не у відстежуваних файлах, у `certs/` немає приватних ключів (phase8 AC-23, ADR-0006) |
| `check_delays.sh` | періодичні задачі без `vTaskDelay` — лише `vTaskDelayUntil` (phase3-crsf AC-19, constitution п. 14); виняток — очікування готовності регістра в `battery_hal.c` і `tof_hal.c` |
| `check_calib.sh` | константи `encoders_calib.h` і `tof_calib.h` (`TOF_OBSTACLE_MM`) мають рядок у `docs/measured.md` з тим самим значенням (phase4-encoders AC-11, phase6-tof AC-18, constitution пп. 3, 13) |
| `check_power_limit.sh` | межа потужності `DRIVE_PWM_MAX_PERMILLE` — рівно 750 у `drive_core.h`, інших визначень межі ШІМ у компонентах немає (phase5-pid AC-09, AC-19) |

`selftest_tools.sh` перевіряє самі скрипти на навмисно поганих прикладах у тимчасовому каталозі.
