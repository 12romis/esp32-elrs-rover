---
id: T8
title: "Підключити в main старт telemetry і колбек step_status, показати колір індикатора й час кроку в status"
layer: "wiring"
deps: ["T1", "T4", "T6", "T7"]
blocks: ["T9", "T11"]
acs: ["AC-04", "AC-05"]
files_hint: ["main/main.c", "main/CMakeLists.txt", "components/rover_console/console_core.c", "components/rover_console/console_hal.c", "components/rover_console/include/console.h", "test/host/test_console.c"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "done"
step: "GATE (без коміту: CLAUDE.md)"
---

# T8 — Підключити в main старт telemetry і колбек step_status, показати колір індикатора й час кроку в status

## Place in the sequence

- **Blocked by:** T1 — Створити telemetry_core: кадр датчика батареї, рішення «слати чи ні», детектор зміни збою, статистика часу кроку й рядок status, T4 — Додати в drive_status_t прапорці секторів і дві відмітки часу навколо кроку control з записом у telemetry_step_record, T6 — Створити telemetry_hal: задача telemetry (200 мс) читає знімок напруги й передає кадр через crsf_hal_send, T7 — Підключити в ui_hal прапорці секторів, жовтий колір індикатора й колір у ui_hal_status_t · **Blocks:** T9 — Написати README (опис, збірка, запуск, інтерфейси) і блок-схему в docs/architecture.md, T11 — Прогнати повну збірку, хостові тести й статичні перевірки, виправити порушення · **Wave:** 4.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** командою `status` бачити, скільки триває крок керування і чи не перевищує він свій період
> **So that** я відрізняю перевантаження процесора від проблем регулятора й заліза
>
> — `spec.md §4, US-02, verbatim` · full text: [spec.md](../spec.md)

Задача показує Розробнику час кроку керування й колір індикатора в `status`.

## Inlined context

> A4. «Час такту керування» — тривалість виконання одного кроку керування (від початку до кінця його роботи, без очікування наступного періоду). Статистика від старту плати: останнє, середнє й найбільше значення в мікросекундах і число кроків, що тривали довше за період 20 мс. Показується лише в `status`; на пульт не йде, бо стандартний кадр CRSF не має для цього поля (розбіжність з документом 02 §7, див. нижче).
>
> — `spec.md §1, A4, verbatim` · full text: [spec.md](../spec.md)

> ├── rover_console/        status: рядок кроку керування; console_ui_status_t і формат status показують колір індикатора (ui_status у main.c мапить enum); console_ops_t.step_status
>
> — `sad.md §5, «Internal decomposition», verbatim` · full text: [sad.md](../sad.md)

> main/main.c               + telemetry_start, колбек step_status, мапінг кольору індикатора в ui_status
>
> — `sad.md §5, «Internal decomposition», verbatim` · full text: [sad.md](../sad.md)

> **Chosen:** варіант 1. Відмітки беруться на початку циклу після пробудження й перед `esp_task_wdt_reset` (вимір кроку без очікування наступного періоду). `telemetry_step_record` — короткий критичний блок із чотирма полями; читає `main` для рядка `status` через новий колбек `step_status`. До першого виміру рядок каже «вимірів ще немає».
>
> — `adr/0003-measure-the-control-step-in-drive-hal-and-keep-the-statistics-in-telemetry-core.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-04 — happy path

> **Given** ровер працює щонайменше 60 с, частину часу озброєний і їде
> **When** Розробник вводить команду `status`
> **Then** вона показує для кроку керування останній, середній і найбільший час виконання в мікросекундах, період кроку та число кроків, що тривали довше за період
>
> — `spec.md §5, AC-04, verbatim` · full text: [spec.md](../spec.md)

### AC-05 — error

> **Given** плата щойно стартувала і жодного кроку керування ще не виконано
> **When** Розробник вводить `status`
> **Then** замість нулів команда каже, що вимірів ще немає
>
> — `spec.md §5, AC-05, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] `console.h`: `size_t (*step_status)(char *buf, size_t n)` у `console_ops_t`; `console_ui_status_t`: колір індикатора (enum) замість прапорця `indicator_red`
- [x] `console_core.c`/`console_hal.c`: рядок кроку керування й колір у `status`; перейти з `indicator_red` на колір, після чого прибрати `indicator_red` з `ui_hal_status_t` і з мапінгу в `main.c`
- [x] `main.c`: `telemetry_start()` після `battery_start`/`crsf_start`, колбек `step_status`; `main/CMakeLists.txt`: REQUIRES `telemetry`
- [x] Оновити `test/host/test_console.c` під нові рядки status

## Edge cases

| Case | Behaviour |
|---|---|
| status до першого кроку | «вимірів ще немає» |
| Індикатор погашений / жовтий / червоний | status називає колір |
| telemetry не стартувала | status це пише, решта status працює |

## Definition of Done

- [x] хостові тести консолі проходять, збірка без нових попереджень, `help` без змін у наборі команд
- [x] кожне Hard Rule вище лишається дійсним (інваріанти п. 7 конституції не зачеплено)
- [x] збірка без нових попереджень
