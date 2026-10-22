---
id: T0
title: "Створити каркаси компонентів session і report: CMake, заголовки й порожні заглушки core/hal, що збираються без попереджень"
layer: "wiring"
deps: []
blocks: ["T5", "T8"]
acs: ["AC-26"]
files_hint: ["components/session/CMakeLists.txt", "components/session/include/", "components/session/session_core.c", "components/session/session_hal.c", "components/report/CMakeLists.txt", "components/report/include/", "components/report/report_core.c", "components/report/mqtt_core.c", "components/report/report_queue_core.c", "components/report/report_store_hal.c", "components/report/report_net_hal.c", "components/report/report_hal.c"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T0 — Створити каркаси компонентів session і report: CMake, заголовки й порожні заглушки core/hal, що збираються без попереджень

## Place in the sequence

- **Blocked by:** — · **Blocks:** T5 — Створити session_core: межі сесії за станом, причина завершення, напруга, утримання за датчиками, час із вимкненим контролем, номер і тривалість, T8 — Створити mqtt_core: кодування CONNECT, PUBLISH (QoS 1), DISCONNECT і розбір CONNACK та PUBACK для MQTT 3.1.1 · **Wave:** 1 (Перша задача фази: створює каркас, щоб T5–T13 не ділили один CMakeLists.).
- **Lane:** shares `components/session/session_core.c` with T5, T6, T7, T8, T9, T10, T11, T12, T13 — serialized.

## Why (user story)

> **As a** Агент
> **I want** збирати прошивку й запускати хостові тести логіки сесії, статистики, черги й режиму контролю
> **So that** я підтверджую правила без заліза й не пускаю в збірку нові попередження
>
> — `spec.md §4, US-09, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: створити каркаси компонентів session і report: CMake, заголовки й порожні заглушки core/hal, що збираються без попереджень.

## Inlined context

> Конституція, п. 6: кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`; логіка живе в core і покривається тестами на хості. Пп. 8–9: збірка без помилок і без нових попереджень; коментарі й коміти — українською, ідентифікатори — англійською.
>
> — `docs/constitution.md, пп. 6, 8, 9, abridged` · full text: [constitution.md](../../../constitution.md)

> Нових компонентів два: `session` (статистика та межі сесії) і `report` (документ, пакети MQTT, черга, HAL мережі й NVS). Файли `report`: `report_core.c` (документ), `mqtt_core.c` (пакети), `report_queue_core.c` (черга й правила спроби), `report_store_hal.c` (NVS), `report_net_hal.c` (Wi-Fi, TLS, обмін), `report_hal.c` (задача). Задача T0 створює лише порожні, що збираються, заглушки, щоб наступні задачі не ділили один CMakeLists.
>
> — `sad.md §5, «Internal decomposition», abridged` · full text: [sad.md](../sad.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-26 — happy path

> **Given** логіка сесії, статистики, черги звітів і режиму контролю реалізована в частинах без заголовків ESP-IDF
> **When** Агент запускає хостові тести й збирає прошивку
> **Then** усі хостові тести проходять, збірка завершується без помилок і без нових попереджень
>
> — `spec.md §5, AC-26, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Створити `components/session/CMakeLists.txt` (SRCS `session_core.c` `session_hal.c`, INCLUDE_DIRS `include`, REQUIRES `app_state` `battery`, PRIV_REQUIRES `freertos` `log`) і порожні `session_core.h/.c`, `session_hal.h/.c`.
- [ ] Створити `components/report/CMakeLists.txt` (SRCS усіх шести файлів вище, REQUIRES `session` `app_state` `battery`, PRIV_REQUIRES `nvs_flash` `esp_wifi` `esp-tls` `esp_netif` `esp_event` `freertos` `log` `esp_app_format` `esp_timer`) і порожні заголовки/файли з коментарем призначення українською. Умова `if(EXISTS)` для сертифіката й секретів додається в T11.
- [ ] Додати `session` і `report` у `main/CMakeLists.txt` REQUIRES (файл не захищений).
- [ ] `idf.py build` без нових попереджень; `test/host/run_tests.sh` збирає порожні `*_core.c` без помилок.

## Edge cases

| Case | Behaviour |
|---|---|
| Заглушки | порожні функції не викликаються, нічого не змінюють у поведінці прошивки |

## Definition of Done

- [ ] `idf.py build` проходить без нових попереджень
- [ ] `test/host/run_tests.sh` і `test/host/check_core_headers.sh` проходять
- [ ] кожне Hard Rule вище лишається дійсним
