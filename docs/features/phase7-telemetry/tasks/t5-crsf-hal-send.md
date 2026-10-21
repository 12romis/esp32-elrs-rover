---
id: T5
title: "Додати crsf_hal_send: запис готового кадру в UART2 власником лінії без блокування керування"
layer: "infra"
deps: []
blocks: ["T6"]
acs: ["AC-03"]
files_hint: ["components/crsf/crsf_hal.c", "components/crsf/include/crsf_hal.h"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "review"
step: "GATE (без коміту: CLAUDE.md)"
---

# T5 — Додати crsf_hal_send: запис готового кадру в UART2 власником лінії без блокування керування

## Place in the sequence

- **Blocked by:** — · **Blocks:** T6 — Створити telemetry_hal: задача telemetry (200 мс) читає знімок напруги й передає кадр через crsf_hal_send · **Wave:** 1.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** бачити на екрані телеметрії пульта напругу акумулятора ровера
> **So that** я помічаю розряд до того, як спрацює відсічка, не підключаючи USB
>
> — `spec.md §4, US-01, verbatim` · full text: [spec.md](../spec.md)

> **As a** Розробник
> **I want** щоб індикація й телеметрія не змінювали інваріантів безпеки, не затримували керування й не ламалися від відмови розширювача портів чи приймача
> **So that** корисна «лампочка» не стає ще одним способом зламати ровер
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Задача дає телеметрії безпечний транспорт: один власник UART2, збій повертається значенням, а не зависанням.

## Inlined context

> **Chosen:** варіант 1. `telemetry_core` будує кадр (тип датчика батареї: напруга з розв'язністю 0.1 В, струм, ємність і відсоток — нулі; CRC8 через `crsf_crc8`) вирішує, чи слати (знімок придатний за `battery_snapshot_usable`), і виявляє зміну «збій / відновлення» передачі (чиста логіка, тест на хості). `telemetry_hal` тримає задачу (core 0, пріоритет 2, `vTaskDelayUntil`, 200 мс), читає `battery_get_snapshot`, викликає `crsf_hal_send` і лише пише `ESP_LOGW` на зміну, яку повідомив core. `crsf_hal_send` повертає false, якщо драйвер не встановлено чи запис не вдався, і ніколи не блокує довше за запис кількох байтів у FIFO.
>
> — `adr/0001-send-battery-telemetry-as-a-crsf-battery-frame-from-a-telemetry-task-through-the-crsf-uart-owner.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> UART2 до приймача: 420000 бод 8N1 (журнал вимірювань, P5); RX = вхід CRSF, TX = лінія `/ELRS_TX` до J7 (нетліст) і вже налаштована `crsf_hal.c`; драйвер встановлено без передавального буфера. Формат кадру CRSF: синхробайт, довжина, тип, корисне навантаження, CRC8 (`crsf_crc8` у `crsf_core.c`).
>
> — `sad.md §2, «Technical», verbatim` · full text: [sad.md](../sad.md)

> **Також (дослівно зі spec §6):** вплив виміру такту на період керування — 0 нових блокуючих викликів, період 20 мс тримається очікуванням до моменту (огляд коду; `check_delays.sh`); шина периферії 100 кГц (`check_bus_rules.sh`).
>
> — `sad.md §10, QG-1, verbatim` · full text: [sad.md](../sad.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-03 — domain invariant

> **Given** приймач відʼєднаний чи лінія до нього не працює, або телеметрія передається в будь-якому стані автомата
> **When** Розробник їде, зупиняється, роззброює ровер чи пульт втрачає звʼязок
> **Then** стан автомата, мотори, світло й затримка «кадр пульта → борти» поводяться так само, як без телеметрії; відмова передачі не змінює стану ровера і не блокує озброєння
>
> — `spec.md §5, AC-03, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] `crsf_hal.h`: `bool crsf_hal_send(const uint8_t *frame, size_t len)` з коментарем українською
- [x] `crsf_hal.c`: false, якщо драйвер не встановлено (`crsf_hal_running()`), `len` 0 чи більше `CRSF_FRAME_MAX`, або запис не вдався; інакше `uart_write_bytes`; без очікування довше за запис у FIFO

## Edge cases

| Case | Behaviour |
|---|---|
| Драйвер UART не встановлено (приймач недоступний) | повертає false, нічого не пише |
| Кадр завеликий або порожній | повертає false |
| Запис не вдався | повертає false; ровер не змінює стан |

## Definition of Done

- [ ] збірка без нових попереджень; `check_delays.sh` і `check_pins.sh` чисті; запис на залізі — T12 (приймання на залізі: acceptance.md)
- [x] кожне Hard Rule вище лишається дійсним (інваріанти п. 7 конституції не зачеплено)
- [x] збірка без нових попереджень
