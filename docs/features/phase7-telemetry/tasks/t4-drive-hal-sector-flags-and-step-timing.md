---
id: T4
title: "Додати в drive_status_t прапорці секторів і дві відмітки часу навколо кроку control з записом у telemetry_step_record"
layer: "infra"
deps: ["T1"]
blocks: ["T7", "T8"]
acs: ["AC-03", "AC-04", "AC-14"]
files_hint: ["components/drive/drive_hal.c", "components/drive/include/drive_hal.h", "components/drive/CMakeLists.txt"]
owner: "Агент"
estimate: "S"
context_budget: "M"
status: "review"
step: "GATE (без коміту: CLAUDE.md)"
---

# T4 — Додати в drive_status_t прапорці секторів і дві відмітки часу навколо кроку control з записом у telemetry_step_record

## Place in the sequence

- **Blocked by:** T1 — Створити telemetry_core: кадр датчика батареї, рішення «слати чи ні», детектор зміни збою, статистика часу кроку й рядок status · **Blocks:** T7 — Підключити в ui_hal прапорці секторів, жовтий колір індикатора й колір у ui_hal_status_t, T8 — Підключити в main старт telemetry і колбек step_status, показати колір індикатора й час кроку в status · **Wave:** 2.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** командою `status` бачити, скільки триває крок керування і чи не перевищує він свій період
> **So that** я відрізняю перевантаження процесора від проблем регулятора й заліза
>
> — `spec.md §4, US-02, verbatim` · full text: [spec.md](../spec.md)

> **As a** Розробник
> **I want** щоб індикація й телеметрія не змінювали інваріантів безпеки, не затримували керування й не ламалися від відмови розширювача портів чи приймача
> **So that** корисна «лампочка» не стає ще одним способом зламати ровер
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Задача дає двом історіям джерело даних: прапорці секторів для індикації та час кроку для status.

## Inlined context

> **Chosen:** варіант 1. Відмітки беруться на початку циклу після пробудження й перед `esp_task_wdt_reset` (вимір кроку без очікування наступного періоду). `telemetry_step_record` — короткий критичний блок із чотирма полями; читає `main` для рядка `status` через новий колбек `step_status`. До першого виміру рядок каже «вимірів ще немає».
>
> — `adr/0003-measure-the-control-step-in-drive-hal-and-keep-the-statistics-in-telemetry-core.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> ├── drive/                + drive_status_t.occ_front/occ_rear (копія ctl.occ_* без умови стану, її накладає ui_core: блимання лише в OBSTACLE_HOLD); дві відмітки часу навколо кроку control: перша після пробудження, друга безпосередньо перед esp_task_wdt_reset; telemetry_step_record викликає drive_hal між другою відміткою і reset
>
> — `sad.md §5, «Internal decomposition», verbatim` · full text: [sad.md](../sad.md)

> | Затримка «кадр пульта → борти» | ≤ 20 мс максимум, зокрема з телеметрією й виміром такту (фаза 4: макс 15 мс) | діагностичні мітки часу фази 3 в журналі монітора |
>
> — `spec.md §6, «Затримка кадр → борти», verbatim` · full text: [spec.md](../spec.md)

> | Вплив виміру такту на період керування | 0 нових блокуючих викликів; період 20 мс тримається очікуванням до моменту | огляд коду; `check_delays.sh` |
>
> — `spec.md §6, «Вплив виміру такту», verbatim` · full text: [spec.md](../spec.md)

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

### AC-04 — happy path

> **Given** ровер працює щонайменше 60 с, частину часу озброєний і їде
> **When** Розробник вводить команду `status`
> **Then** вона показує для кроку керування останній, середній і найбільший час виконання в мікросекундах, період кроку та число кроків, що тривали довше за період
>
> — `spec.md §5, AC-04, verbatim` · full text: [spec.md](../spec.md)

### AC-14 — domain invariant

> **Given** ровер в OBSTACLE_HOLD із повною індикацією
> **When** пульт втрачає звʼязок довше за допустиме, або напруга падає нижче відсічки, або Розробник роззброює ровер
> **Then** мотори отримують нуль і STBY опускається в тих самих межах часу, що й без індикації; індикація не затримує реакції і не вмикає STBY
>
> — `spec.md §5, AC-14, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] `drive_hal.h`: у `drive_status_t` поля `occ_front`, `occ_rear` (копія `ctl.occ_*` без умови стану)
- [x] `drive_hal.c` у `control_task`: перша відмітка `esp_timer_get_time()` після пробудження, друга безпосередньо перед `esp_task_wdt_reset`; виклик `telemetry_step_record(us, period_us)` між другою відміткою й reset; прапорці у `s_status` під `portMUX`
- [x] `components/drive/CMakeLists.txt`: REQUIRES `telemetry`

## Edge cases

| Case | Behaviour |
|---|---|
| Крок довший за 20 мс | вимір записується; період керування не змінюється |
| telemetry не стартувала | запис виміру безпечний (статистика лише накопичується) |
| Нема свіжого знімка далекомірів | прапорці секторів хибні, як і в `ctl.occ_*` |

## Definition of Done

- [ ] збірка без нових попереджень; затримка «кадр → борти» в межах spec §6 (приймання T12); `check_delays.sh` чистий (приймання на залізі: acceptance.md)
- [x] кожне Hard Rule вище лишається дійсним (інваріанти п. 7 конституції не зачеплено)
- [x] збірка без нових попереджень
