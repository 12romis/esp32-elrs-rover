# ESP32-ELRS-Rover — інструкції для агента

Ровер на ESP32-S3 (YD-ESP32-S3 N16R8), ESP-IDF v6.1, C. Контекст: `docs/01-ASSEMBLY-BRINGUP.md`, `docs/02-FIRMWARE-CONTEXT.md`. Виміряні величини: `docs/measured.md`. Джерело істини про залізо: `kicad-rover/kicad-rover.net`.

## Команди
- Збірка основної прошивки: `idf.py build` (корінь; ціль `esp32s3`; перед цим `source ~/.espressif/tools/activate_idf_v6.1.sh`).
- Діагностичні прошивки P0–P7: окремий проєкт `tools/bringup/` (перемикач `BRINGUP_TEST` у `main.c`), збирається там само.
- Тести чистої логіки: хостовий раннер у `test/host/` (звичайний gcc).
- Прошивку на плату користувач робить сам через розширення ESP-IDF у VS Code; агент лише збирає.

## Конституція (не обговорюється агентом на жодній стадії)

@docs/constitution.md

## Порядок роботи
- Передфази P0–P7 — поза циклом SDD: діагностичний код, результат у `docs/measured.md`.
- Далі одна фаза = одна «фіча» зі слагом (`phase0-skeleton`, `phase1-drive`, …); специфікації в `docs/features/<slug>/spec.md`.
- Не комітити й не пушити без прямої вказівки користувача.
