## Summary

Фаза 0: безпечна основа прошивки — старт до DISARMED, консоль через USB, перевірений сторожовий таймер, автомат станів із таблицею переходів, перевіреною на хості. Мотори, STBY, шина периферії не задіяні. [spec](docs/features/phase0-skeleton/spec.md)

## Acceptance criteria

- AC-01 — старт BOOT → SELFTEST → DISARMED з журналом і історією в `status` ✓ (хост + збірка, залізо чекає)
- AC-02 — `status` і `help` ✓
- AC-03 — невідома команда, порожній і задовгий рядок, зайвий аргумент ✓ (хост)
- AC-04 — `wdt-test` → перезавантаження з причиною TASK_WDT (логіка ✓, залізо чекає)
- AC-05 — права команд за станом ✓ (хост)
- AC-06 — таблиця 10 із 49, відмова з іменами станів ✓ (хост)
- AC-07 — жодних ліній моторів і шини периферії ✓ (огляд коду, мультиметр чекає)
- AC-08, AC-09 — збірка без нових попереджень, core без заголовків ESP-IDF ✓
- AC-10, AC-11 — захищені файли, номери GPIO лише в `board_pins.h` ✓ (скрипти)
- AC-12 — очищення сховища лише за двох результатів ✓ (хост; залізо чекає OQ-6)
- AC-13 — `reboot` ✓

## Design

- Spec: `docs/features/phase0-skeleton/spec.md`
- Architecture: `docs/features/phase0-skeleton/sad.md`
- Decisions: `docs/features/phase0-skeleton/adr/` (0001–0004)
- Data model / API: немає (поверхня `cli`)

## Tasks (SDD-Task trailers)

T1 хостовий раннер · T2 app_state core · T3 rover_console core · T4 storage/watchdog core · T5 app_state/storage HAL · T6 watchdog HAL · T7 console HAL · T8 main · T9 чек-лист приймання; плюс виправлення за review. `git log --grep SDD-Task`.

## Verification

- Unit: `test/host/run_tests.sh` — 456 тестів, усі пройдені; `selftest_tools.sh` чисто.
- Build: `idf.py build` з чистого стану — 0 помилок, 0 попереджень компілятора (5 CMake-попереджень самого IDF — baseline).
- Lint/vet: окремих лінтерів немає; `check_core_headers.sh`, `check_pins.sh`, `check_protected.sh` чисті.
- Ran the feature: логіку core прогнано на хості (демо-збірка): старт до DISARMED, відмова BOOT-подібного переходу, `foo`/`HELP`/`status foo`/пробіли/65 символів, права в ARMED, рішення сховища. **Не виконано:** прошивання й запуск на платі (консоль USB, TWDT, STBY) — за `docs/features/phase0-skeleton/acceptance.md`.

## Operational notes

- Migration: none. Config: `sdkconfig.defaults` не змінено.
- Rollback: прошити попередній образ / revert PR.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
