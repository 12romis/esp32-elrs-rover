## Summary

Фаза 8: тумблер CH9 вмикає/вимикає контроль перешкод (зеленуватий індикатор), консоль називає всі датчики, що бачать перешкоду, а після
роззброєння ровер публікує звіт сесії (JSON, MQTT 3.1.1 поверх TLS) лише в DISARMED; недоставлені звіти (≤ 2) зберігаються в NVS.

[spec](docs/features/phase8-session-report/spec.md) · [SAD](docs/features/phase8-session-report/sad.md) ·
[ADR-0001…0006](docs/features/phase8-session-report/adr/) · [changelog](docs/features/phase8-session-report/changelog.md) ·
[рев'ю](docs/features/phase8-session-report/_review/review-2026-10-22.md)

## Коміти
1. Узгодження хостових тестів і `check_power_limit.sh` з `bc4aedd` (не частина фази).
2. Код і тести фази 8 (задачі T0–T15).
3. Виправлення за результатами приймання: адреса сервера зі схемою, діагностика Wi-Fi, рівні логів.
4. Документація, журнал задач, приймання, changelog.

## Verification
- `test/host/run_tests.sh`: 15610 тестів пройдено; усі `check_*.sh` (з `ALLOW_PROTECTED=1`) і `selftest_tools.sh` чисті.
- `idf.py build`: 0 помилок і 0 попереджень компіляції (5 попереджень CMake бібліотек ESP-IDF були й до змін); окрема збірка з Wi-Fi/TLS-гілкою — теж.
- На залізі Розробник підтвердив доставку звітів (AC-10, AC-16), невдачу з черги (AC-14), витіснення (AC-15), CH9 і рядок з усіма датчиками (AC-01, AC-02, AC-08); деталі в [changelog.md](docs/features/phase8-session-report/changelog.md).
- **Ще не перевірено на залізі**: AC-18, AC-13, AC-22, AC-24, пробіг 1 м, OQ-5/OQ-6/OQ-7 — [acceptance.md](docs/features/phase8-session-report/acceptance.md).

## Захищені файли
Змінено лише `sdkconfig.defaults` (дозвіл Розробника 2026-10-22), див. [protected-changes.md](docs/features/phase8-session-report/protected-changes.md).

## Відкрито
OQ-5 (TP3 під Wi-Fi), OQ-6 (положення CH9), OQ-7 (переривання озброєнням під час TCP/DNS). `report_secrets.h` і `root_ca.pem` надає Розробник.
