## Summary

Фаза 1: керування моторами з консолі — `arm`, `drive <тяга> <поворот>`, `disarm`; мікшер із обмеженням 0.75, команда руху 2 с з авто-роззброєнням, відсічка по напрузі 6.0 В (ARMED → FAILSAFE), рядок «остання зупинка» в `status`. Нові компоненти `drive` і `battery` (core + HAL). [spec](docs/features/phase1-drive/spec.md)

## Acceptance criteria

- AC-01, AC-02 — `arm`, `disarm` ✓ (хост + залізо)
- AC-03, AC-04, AC-10, AC-21 — мікшер, вирівнювання, обмеження 0.75, нуль на борту ✓ (хост + залізо)
- AC-05, AC-06, AC-07 — відмови: числа, стан ✓ (хост + залізо)
- AC-08, AC-20 — STBY лише в ARMED, вибіг, виходи раніше за стан ✓ (хост; STBY 0 В виміряно)
- AC-09 — `disarm` у будь-якому стані з поясненням ✓ (хост + залізо)
- AC-11, AC-12 — вікно 2 с, зупинка без хоста, «остання зупинка» після підключення ✓ (залізо)
- AC-14 — відсічка в русі ✓ хост; на залізі лише відʼєднанням акумулятора (плавне зниження чекає блоку живлення)
- AC-16 — `status`, `help` ✓
- AC-17, AC-18, AC-19 — збірка без попереджень, core без заголовків IDF, межі Агента ✓
- **AC-13, AC-15 (низька напруга на старті), AC-22 — чекають** другої точки калібрування (spec OQ-1) і блоку живлення

## Design

- Spec: `docs/features/phase1-drive/spec.md`
- Architecture: `docs/features/phase1-drive/sad.md`
- Decisions: `docs/features/phase1-drive/adr/` (0001–0004)
- Review: `docs/features/phase1-drive/_review/review-2026-10-18.md` (8 знахідок, усі виправлено)
- Data model / API: немає (поверхня `cli`)

## Tasks

T1 battery_core · T2 drive_core: мікшер · T3 drive_ctl: запити · T4 drive_ctl: вікно і відсічка · T5 console_core · T6 battery_hal · T7 drive_hal · T8 console_hal · T9 main · T10 чек-лист приймання. Комітів із трейлерами `SDD-Task` немає: за вказівкою Розробника зміни зібрано п'ятьма тематичними комітами.

## Verification

- Unit: `test/host/run_tests.sh` — 1325 тестів, усі пройдені; `check_core_headers.sh`, `check_pins.sh`, `check_protected.sh` чисті.
- Build: `idf.py build` — 0 помилок, 0 попереджень компілятора (5 CMake-попереджень самого IDF — baseline фази 0).
- Ran the feature (Розробник на платі, колеса в повітрі, 2026-10-18): AC-03 — `arm`, `drive 0.3 0` → «ліве 0.30, праве 0.30», далі авто-роззброєння; AC-04 — `drive 0.8 0.5` → «ліве 0.75, праве 0.23»; AC-02 — `disarm` під час руху, мотори зупинились швидко; AC-12 — відʼєднання USB під час руху, після підключення `status` показує «остання зупинка: команда руху спливла…; мінімальна напруга: 7713 мВ»; AC-09 — `disarm` у DISARMED відповідає з поясненням; серія з 10 циклів без збоїв. **Не виконано:** AC-13, AC-15, AC-22 (немає блоку живлення), плавне зниження напруги для AC-14.

## Operational notes

- Migration: none. Config: `sdkconfig.defaults` не змінено; калібрування напруги попереднє (`battery_calib.h`).
- Rollback: прошити попередній образ / revert PR.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
