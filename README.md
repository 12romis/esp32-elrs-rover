# ESP32-ELRS-Rover

Ровер на ESP32-S3 (N16R8) з керуванням через ExpressLRS/CRSF. ESP-IDF, C.

- `docs/` — контекст заліза й прошивки
- `kicad-rover/` — схема й плата
- `main/`, `components/` — прошивка (структура: `docs/02-FIRMWARE-CONTEXT.md` §6.4)
- `test/host/` — тести чистої логіки (`*_core.c`), звичайний gcc
- `tools/bringup/` — діагностичні прошивки P0–P7

Збірка і прошивка — через розширення ESP-IDF у VS Code (ціль `esp32s3`).
