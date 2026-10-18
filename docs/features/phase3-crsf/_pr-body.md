## Summary

Фаза 3: керування з пульта ELRS. Прошивка приймає кадри каналів CRSF (UART2, 420000 бод) і рухає борти правим стіком. Озброєння — фронтом тумблера за нейтралі, живого звʼязку й напруги ≥ 6.0 В. Втрата кадрів понад 500 мс в ARMED дає FAILSAFE («втрата звʼязку»), вихід з нього — після ≥ 1 с стабільного звʼязку й тумблера в DISARM. Без пульта працює стендовий режим з консольними `arm`/`drive`. Специфікація — [spec](docs/features/phase3-crsf/spec.md), список змін — [changelog](docs/features/phase3-crsf/changelog.md).

## Acceptance criteria

- AC-01, AC-02 — стік → знаки бортів, мертва зона, обмеження 0.75 ✓ (хост + залізо)
- AC-03, AC-04, AC-16 — озброєння фронтом тумблера, відмови по одному рядку, роззброєння тумблером і `disarm` ✓ (хост + залізо)
- AC-05, AC-06, AC-07 — втрата звʼязку ≤ 520 мс → FAILSAFE; 0x14 і биті кадри таймер не скидають; у DISARMED без FAILSAFE ✓ (хост + залізо, 513–515 мс)
- AC-08, AC-09, AC-17 — вихід із FAILSAFE за причиною, відсічка по напрузі назавжди ✓ (хост + залізо)
- AC-10, AC-11 — шум і биті кадри, обрізання діапазону ✓ (хост + залізо, крок 10)
- AC-12, AC-13, AC-15 — `status`, стенд, «керує пульт» ✓ (хост + залізо)
- AC-14 — поява пульта під час консольного руху ✓ **лише хост** (на залізі пульт зʼявився вже в DISARMED)
- AC-18, AC-19, AC-20 — раннер, перевірки конституції, приймання Розробником ✓

## Design

- Spec: `docs/features/phase3-crsf/spec.md` · Architecture: `docs/features/phase3-crsf/sad.md` · Contract: `docs/features/phase3-crsf/contracts/cli.md`
- Decisions: `docs/features/phase3-crsf/adr/` (0001–0003)
- Review: `docs/features/phase3-crsf/_review/review-2026-10-18.md` (F1–F6, CHANGES REQUESTED) → `review-2026-10-18-r2.md` (PASS)
- Data model / API: немає (поверхня `cli`)

## Tasks (SDD-Task trailers)

T1 crsf_core: збирач кадрів, CRC8 · T2 намір пілота · T3 app_state: причина FAILSAFE · T4 стенд, таймер звʼязку · T5 озброєння тумблером, рух стіком · T6 вихід із FAILSAFE · T7 crsf_hal UART2 · T8 ui_core вихід із FAILSAFE · T9 drive_hal і main · T10 консоль · T11 матриця 126 пар, check_delays · T12 чекліст приймання · T13–T18 виправлення огляду (беззнакові мітки, тумблер «невідомо», пояснення FAILSAFE за причиною, «стенд» у `arm`, без WDT немає моторів, журнал затримки «кадр → борти»).

## Verification

- Unit: `test/host/run_tests.sh` — 5388 тестів пройдено; покриття 49/49 пар станів, 63/63, 126/126 «команда × стан × режим», 21/21; `check_core_headers.sh`, `check_pins.sh`, `check_delays.sh`, `check_bus_rules.sh`, `check_protected.sh` — rc=0.
- Build: `idf.py build` — rc=0, 0 попереджень компілятора.
- Ran the feature (Розробник на платі, 2026-10-18, кроки 1–12 `acceptance.md`, прошивка до T13–T18):
  - AC-05 — пульт вимкнено в русі → FAILSAFE, втрату виявлено на 513–515 мс від останнього кадру;
  - AC-04 — відхилений стік і фронт тумблера → один рядок «стік не в нейтралі», без озброєння;
  - AC-16 — `disarm` в ARMED з тумблером у ARM → підказка повернути тумблер, повторного озброєння немає;
  - AC-12 — `status` в ARMED: 18358 кадрів, 0 CRC, вільна памʼять 358843 байт.
- Не перевірено на залізі: крок 13 (NFR «стік → борти ≤ 50 мс»), B − A кроку 7, AC-14 під час руху; виправлення T13–T18 повторно на платі не прогнані.

## Operational notes

- Migration: none. Config: `sdkconfig.defaults` і корінь `CMakeLists.txt` не змінено.
- Калібрування напруги: двоточкове, k 3.973, зсув +151 мВ (P3).
- Rollback: прошити попередній образ / revert PR.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
