## Summary

Фаза 7: телеметрія й індикація. Ровер раз на 200 мс шле на пульт напругу акумулятора (кадр датчика батареї CRSF, 0.1 В; струм, ємність, відсоток — «немає даних»). У OBSTACLE_HOLD: подвійний біп на вході й на виході в ARMED, жовтий індикатор плати, блимання фар (передній сектор) і задніх ліхтарів (задній). `status` показує час кроку керування. Додано README, блок-схему й чекліст самоперевірки.

## Acceptance criteria

- AC-17, AC-18, AC-19, AC-20, AC-21 ✓ (документи, хост, межі роботи Агента)
- AC-01…AC-16 — логіка ✓ (хост), **на залізі не перевірено** — приймання за `acceptance.md` робить Розробник (OQ-1, OQ-3, OQ-4)

## Design

- Spec: `docs/features/phase7-telemetry/spec.md` · Architecture: `sad.md` (два потоки в §6)
- Decisions: `adr/` (0001 — окрема задача й `crsf_hal_send`; 0002 — індикація в `ui_core` зі прапорців `control`, частково замінює ADR-0004 phase6-tof; 0003 — вимір кроку керування)
- Review: `_review/review-2026-10-21.md` (10 зауважень виправлено, PASS)
- Data model / API: немає (поверхня `cli`)

## Tasks

T1 `telemetry_core` · T2–T3 `ui_core` (біп, жовтий, блимання) · T4 `drive_hal` · T5 `crsf_hal_send` · T6 `telemetry_hal` · T7 `ui_hal` · T8 `main` і консоль · T9–T10 README, схема, чекліст · T11 перевірки · T12 `acceptance.md`.

## Verification

- Unit: `test/host/run_tests.sh` — 15125 з 15125; `check_*.sh` (7) і `selftest_tools.sh` — чисті.
- Build: `idf.py build` — 0 помилок, 0 попереджень компіляції; `rover.bin` 0x5a960, 65 % вільно.
- Ran the feature: **на залізі не запускалось**. Хостова демонстрація на реальному коді: AC-01 — кадр для 7400 мВ `C8 0A 08 00 4A 00 00 00 00 00 00 41` (74 десятки); AC-06 — вхід в OBSTACLE_HOLD: звук о 400 і 600 мс, пауза о 500, індикатор жовтий, фари блимають; AC-07 — вихід в ARMED: звук о 1100 і 1300 мс, індикатор погас.

## Operational notes

- Нова задача `telemetry` (core 0, prio 2). Записати вільну пам'ять зі `status` (поріг ≥ 345000 байт) і рядок «крок керування».
- Rollback: revert PR або прошити попередній образ; даних у NVS немає.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
