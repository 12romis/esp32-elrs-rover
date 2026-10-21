## Summary

Фаза 6: далекоміри й протизіткнення. Чотири VL53L0X стають двома секторами; сектор ближче за 170 мм зайнятий, тягу в його бік обнулено, поворот на місці й рух у вільний бік лишаються, автомат іде ARMED ↔ OBSTACLE_HOLD. Відмова датчика не зупиняє й не вішає прошивку; `tof` показує стан, `tof reinit` (DISARMED) повертає датчик. [spec](docs/features/phase6-tof/spec.md)

## Acceptance criteria

- AC-03…AC-05, AC-09…AC-11, AC-13, AC-17, AC-18 ✓ (хост)
- AC-01, AC-02, AC-06…AC-08, AC-12, AC-14…AC-16 — логіка ✓ (хост), **на залізі не перевірено** — відкладено Розробником (`acceptance.md`, AC-19)
- AC-19 — межі роботи Агента дотримано (поріг не вигаданий: 170 мм — рішення Розробника в `docs/measured.md`)
- Рішення в процесі: STBY піднімає лише свідома команда руху (spec A10, OQ-3); AC-08 послаблено (пауза опитування під час `tof reinit`); `sensors` не в Task WDT (override у SAD §1)

## Design

- Spec: `docs/features/phase6-tof/spec.md` · Architecture: `docs/features/phase6-tof/sad.md` (9 потоків у §6)
- Decisions: `docs/features/phase6-tof/adr/` (0001 — обмеження в кроці керування; 0002 — `ioexp` володіє байтом із лініями скидання; 0003 — окрема задача `sensors`; 0004 — OBSTACLE_HOLD як стан руху, 12 пар)
- Review: `docs/features/phase6-tof/_review/review-2026-10-21.md` (два раунди, PASS)
- Data model / API: немає (поверхня `cli`)

## Tasks (SDD-Task trailers)

- T1–T2 — `tof_core`: медіана, сектори, відмова, `tof_limit_sides`, тексти. T3 — предикат «ровер їде» і 12-та пара. T4, T9 — байт розширювача з лініями скидання.
- T5–T6 — `drive_core`: знімок, обмеження, переходи, паритет інваріантів ARMED в OBSTACLE_HOLD. T7 — `ui`. T8 — команда `tof` і `drive` в OBSTACLE_HOLD.
- T10–T11 — `tof_hal`: драйвер VL53L0X, послідовність XSHUT, задача `sensors`, `tof reinit`. T12–T13 — `drive_hal`, `main`. T14 — запобіжники й паралельний раннер. T15 — `acceptance.md`.
- Після рев'ю: R1–R14 (коміти `review: phase6-tof …`).

## Verification

- Unit: `test/host/run_tests.sh` — 15001 з 15001, ≈ 0.7 с; `check_*.sh` (7) і `selftest_tools.sh` — чисті.
- Build: `idf.py build` — rc=0, 0 попереджень; `rover.bin` 0x59b40, 65 % вільно.
- Ran the feature: **на залізі не запускалось** (прошивку на плату робить Розробник). Хостова симуляція на реальному коді `tof_core` + `drive_core` (рух до стіни 400 мм): AC-01 — перший відлік < 170 мм на 1700 мс, нуль бортам на 1800 мс (Δ 100 мс ≤ 250); AC-03 — у OBSTACLE_HOLD стік назад → їде −500/−500, повернення в ARMED після звільнення; AC-04 — вперед+вбік → 400/−400 (розворот на місці); AC-15 — пояснення про перешкоду один раз; AC-13 — 12 пар із 49 (тест). Фізику (вибіг, шина, датчики) симуляція не моделює.

## Operational notes

- Нова задача `sensors` (стек 4096, core 0, prio 3, без Task WDT); шина 100 кГц. Записати вільну пам'ять зі `status` (поріг ≥ 345000 байт).
- Поріг перешкоди — константа `TOF_OBSTACLE_MM` = 170 у `components/tof/include/tof_calib.h`; змінюється лише після запису в `docs/measured.md`. Розрахунковий зазор після зупинки ≈ 42 мм (OQ-2: ближче 40 мм не виміряно).
- Rollback: revert PR або прошити попередній образ; даних у NVS немає.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
