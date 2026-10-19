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
| `check_protected.sh` | захищені файли не змінені без згоди (AC-10, constitution п. 10–11) |
| `check_delays.sh` | періодичні задачі без `vTaskDelay` — лише `vTaskDelayUntil` (phase3-crsf AC-19, constitution п. 14) |
| `check_calib.sh` | константи `encoders_calib.h` мають рядок у `docs/measured.md` з тим самим значенням (phase4-encoders AC-11, constitution пп. 3, 13) |

`selftest_tools.sh` перевіряє самі скрипти на навмисно поганих прикладах у тимчасовому каталозі.
