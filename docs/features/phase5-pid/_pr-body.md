## Summary

Фаза 5: регулятор швидкості бортів. Крок керування (20 мс) тримає задану швидкість кожного борту за знімком енкодерів фази 4: PID з розгоном, межею потужності 0.75, зупинкою накопичення на межі й відкатом на пряме керування, коли енкодери недоступні. Коефіцієнти змінюються з консолі (`pid kp|ki|kd|ramp`, `pid on|off`, `pid save`, `pid trace`) і зберігаються в NVS одним записом із CRC32. Специфікація — [spec](docs/features/phase5-pid/spec.md), список змін — [changelog](docs/features/phase5-pid/changelog.md).

## Acceptance criteria

- AC-03…AC-09, AC-12, AC-12b, AC-15, AC-16, AC-18, AC-19 ✓ (хост)
- AC-01, AC-02, AC-05, AC-06, AC-10, AC-13, AC-14 — логіка ✓ (хост), **на залізі не перевірено** — відкладено Розробником (`acceptance.md`, AC-20; T7, T8, T10 у `review`)
- AC-11, AC-17 — винесено зі scope (захист від заклинення, підняття межі 0.75)

## Design

- Spec: `docs/features/phase5-pid/spec.md` · Architecture: `docs/features/phase5-pid/sad.md`
- Decisions: `docs/features/phase5-pid/adr/` (0001 — регулятор у кроці керування; 0002 — запис набору з CRC32; 0003 — трасування окремою задачею)
- Review: `docs/features/phase5-pid/_review/` — `review-2026-10-20.md` … `-r5.md` (r5: PASS)
- Data model / API: немає (поверхня `cli`)

## Tasks (SDD-Task trailers)

- T1–T3 — `pid_core`: набір і запис, крок регулятора, трасування і тексти.
- T4–T5 — `drive_core`: регулятор у кроці керування, відкати й зупинки.
- T6 — команда `pid` у консолі. T7 — `pid_hal`. T8 — підключення в `main`.
- T9 — хостові запобіжники (`check_power_limit.sh`). T10 — `acceptance.md`.
- T11–T23 — виправлення рев'ю (F1–F9, N1–N4, R1, S1, S2).

## Verification

- Unit: `test/host/run_tests.sh` — 6704 з 6704, ≈ 4.5 с; покриття 49/49, 63/63, 49/49, 140/140, 21/21. `check_*.sh` (7) і `selftest_tools.sh` — чисті.
- Build: `idf.py build` (новий каталог) — rc=0, 0 попереджень компілятора; `rover.bin` 0x54c70.
- Ran the feature: **на залізі не запускалось** (прошивку на плату робить Розробник). Перевірено лише хостовими тестами на моделі борту: AC-13 (розгін лише на зростання, нуль одразу — `test_pid.c`), AC-09/AC-10 (вихід ≤ 0.75, накопичена складова не росте при утриманні колеса), AC-07 (пошкоджений запис → «не налаштований»).
- Не перевірено на залізі: AC-01, -02, -05, -06, -10, -13, -14 і KPI spec §7; AC-07, -08, -12, -12b відтворити на залізі нічим.

## Operational notes

- `sdkconfig.defaults` і корінь `CMakeLists.txt` не змінено. Нова задача `pid_trace`; нове NVS-значення. З `docs/constitution.md` п. 7 вилучено рядок про межу 0.75 до фази 5 — межу стереже `check_power_limit.sh`.
- Rollback: прошити попередній образ або revert PR.
