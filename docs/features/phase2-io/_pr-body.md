## Summary

Фаза 2: шина периферії, світло, зумер, індикація станів. Перша операція на шині — запис нуля в розширювач; команди `light` і `beep`; задні ліхтарі (червоні в DISARMED, жовті чи зелені в ARMED), фари при вході в ARMED, FAILSAFE: блимання задніх, червоний індикатор плати, періодичний писк; короткий сигнал на переходах DISARMED/ARMED/FAILSAFE; відмова розширювача лише вимикає світло.

## Acceptance criteria

- AC-01, AC-02, AC-03 — старт: нуль першим, світло після старту й перезапуску ✓ (хост + залізо)
- AC-04…AC-07, AC-13 — `light`, помилки, дозволи за станом, `status`, `help` ✓ (хост + залізо)
- AC-08…AC-12 — `beep`, заміна, діапазон, низький рівень зумера, незалежність від руху ✓ (хост + залізо)
- AC-16…AC-18, AC-27 — маска розрядів, задні ліхтарі й фари ✓ (хост + залізо)
- AC-19…AC-23 — FAILSAFE, індикатор, сигнали переходів ✓ (хост + залізо)
- AC-24…AC-26 — збірка, перевірки конституції, межі Агента ✓
- **AC-14, AC-15 — лише хост** (розширювач запаяний, кроки 13–14 не виконані); AC-15 частково підтверджено відʼєднанням акумулятора

## Design

- Spec: `docs/features/phase2-io/spec.md` · Architecture: `docs/features/phase2-io/sad.md` · Contract: `docs/features/phase2-io/contracts/cli.md`
- Decisions: `docs/features/phase2-io/adr/` (0001–0005)
- Review: `docs/features/phase2-io/_review/review-2026-10-18.md` (10 знахідок, усі виправлено; гейт CHANGES REQUESTED закрито повторним прийманням на залізі)
- Data model / API: немає (поверхня `cli`)

## Tasks

T1 i2c_bus_core · T2 ioexp_core · T3 ui_core: ліхтарі й дозволи · T4 ui_core: зумер і сигнали · T5 console_core · T6 i2c_bus і ioexp HAL · T7 bringup P8/P9 · T8 ui_hal · T9 зумер LEDC · T10 ui_calib.h · T11 драйвер індикатора · T12 console_hal і main · T13 хостовий раннер і перевірки · T14 acceptance. Коміти без `SDD-Task` трейлерів (зміни фази зроблено одним проходом).

## Verification

- Unit: `test/host/run_tests.sh` — 2662 тести пройдено; покриття 63/63 пар «команда × стан», 21/21 «канал × стан», 49/49 пар станів; `check_core_headers.sh`, `check_pins.sh`, `check_protected.sh`, `check_bus_rules.sh`, `selftest_tools.sh` чисті.
- Build: `idf.py build` — 0 помилок і 0 попереджень компілятора; `tools/bringup` (P7, P8, P9) збирається чисто.
- Ran the feature (Розробник на платі, колеса в повітрі, 2026-10-18): AC-01/AC-13 — старт, `status` показує розширювач доступний, фари вимкнені, задній червоний; AC-03 — `reboot` при ввімкнених фарах → фари вимкнені; AC-04 — канали незалежні, жовтий; AC-17/AC-27 — після `arm` фари ввімкнені, задні жовті, назад зелені, після роззброєння фари гаснуть; AC-19 — відсічка (відʼєднано акумулятор в ARMED): індикатор червоний, писк, після повернення живлення задні блимають; AC-08/AC-09 — звуки чутно; вільна памʼять 362911 байт.

## Operational notes

- Migration: none. Config: `sdkconfig.defaults` не змінено.
- Rollback: прошити попередній образ / revert PR.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
