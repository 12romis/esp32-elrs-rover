---
id: T6
title: "Створити telemetry_hal: задача telemetry (200 мс) читає знімок напруги й передає кадр через crsf_hal_send"
layer: "infra"
deps: ["T1", "T5"]
blocks: ["T8"]
acs: ["AC-01", "AC-02", "AC-03"]
files_hint: ["components/telemetry/telemetry_hal.c", "components/telemetry/include/telemetry_hal.h", "components/telemetry/CMakeLists.txt"]
owner: "Агент"
estimate: "S"
context_budget: "M"
status: "review"
step: "GATE (без коміту: CLAUDE.md)"
---

# T6 — Створити telemetry_hal: задача telemetry (200 мс) читає знімок напруги й передає кадр через crsf_hal_send

## Place in the sequence

- **Blocked by:** T1 — Створити telemetry_core: кадр датчика батареї, рішення «слати чи ні», детектор зміни збою, статистика часу кроку й рядок status, T5 — Додати crsf_hal_send: запис готового кадру в UART2 власником лінії без блокування керування · **Blocks:** T8 — Підключити в main старт telemetry і колбек step_status, показати колір індикатора й час кроку в status · **Wave:** 2.
- **Lane:** shares `components/telemetry/CMakeLists.txt` with T1 — serialized.

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

Задача виконує історію US-01: кожні 200 мс на пульт іде справжня напруга або нічого.

## Inlined context

> A1. Кадр телеметрії — тип датчика батареї; передається лише напруга з розв'язністю 0.1 В, округлена до найближчого 0.1 В; струм, ємність і відсоток заряду дорівнюють нулю і означають «немає даних» (README це називає). Напруга береться зі знімка компонента батареї, а не з другого вимірювання.
>
> — `spec.md §1, A1, verbatim` · full text: [spec.md](../spec.md)

> A2. Період передачі 200 мс (документ 02 §6.1, таблиця задач). Якщо знімка напруги немає або він застарів, кадр не надсилається, а вигаданого значення немає. Кадри йдуть у всіх станах автомата й у стендовому режимі, доки працює лінія до приймача.
>
> — `spec.md §1, A2, verbatim` · full text: [spec.md](../spec.md)

> A3. Телеметрія нічого не вирішує: помилка передачі або відсутність приймача не змінює стан автомата, моторів і світла й не затримує такт керування; в журнал іде одне попередження на кожну зміну (збій, відновлення), а не на кожен період.
>
> — `spec.md §1, A3, verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. `telemetry_core` будує кадр (тип датчика батареї: напруга з розв'язністю 0.1 В, струм, ємність і відсоток — нулі; CRC8 через `crsf_crc8`) вирішує, чи слати (знімок придатний за `battery_snapshot_usable`), і виявляє зміну «збій / відновлення» передачі (чиста логіка, тест на хості). `telemetry_hal` тримає задачу (core 0, пріоритет 2, `vTaskDelayUntil`, 200 мс), читає `battery_get_snapshot`, викликає `crsf_hal_send` і лише пише `ESP_LOGW` на зміну, яку повідомив core. `crsf_hal_send` повертає false, якщо драйвер не встановлено чи запис не вдався, і ніколи не блокує довше за запис кількох байтів у FIFO.
>
> — `adr/0001-send-battery-telemetry-as-a-crsf-battery-frame-from-a-telemetry-task-through-the-crsf-uart-owner.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> **Critical flow 1: телеметрія напруги**
>
> — `sad.md §6, «Critical flow 1», abridged` · full text: [sad.md](../sad.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-01 — happy path

> **Given** ровер увімкнений, акумулятор підключений, приймач привʼязаний до пульта, а напруга акумулятора виміряна
> **When** Розробник відкриває на пульті екран телеметрії
> **Then** він бачить напругу акумулятора, яка відрізняється від показу `status` не більше ніж на 0.1 В і оновлюється не рідше ніж раз на секунду; струм, ємність і відсоток заряду не несуть даних і описані в README як «немає даних»
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

### AC-02 — error

> **Given** придатної напруги немає: акумулятор ще не виміряно або останній вимір застарів
> **When** настає час передати телеметрію
> **Then** пульт не отримує вигаданого значення (датчик напруги на пульті не оновлюється), а в журналі зʼявляється одне попередження про зміну стану, а не по рядку на кожен період
>
> — `spec.md §5, AC-02, verbatim` · full text: [spec.md](../spec.md)

### AC-03 — domain invariant

> **Given** приймач відʼєднаний чи лінія до нього не працює, або телеметрія передається в будь-якому стані автомата
> **When** Розробник їде, зупиняється, роззброює ровер чи пульт втрачає звʼязок
> **Then** стан автомата, мотори, світло й затримка «кадр пульта → борти» поводяться так само, як без телеметрії; відмова передачі не змінює стану ровера і не блокує озброєння
>
> — `spec.md §5, AC-03, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] `telemetry_hal.h`: `esp_err_t telemetry_start(void)`
- [x] `telemetry_hal.c`: задача (core 0, пріоритет 2, стек 4096, `vTaskDelayUntil` 200 мс), `battery_get_snapshot`, рішення й кадр з `telemetry_core`, `crsf_hal_send`; `ESP_LOGW` лише на зміну, яку повідомив core; підписка на Task WDT з логом помилки, як у `ui_hal.c`
- [x] `CMakeLists.txt`: SRCS `telemetry_hal.c`, REQUIRES `battery` `crsf`

## Edge cases

| Case | Behaviour |
|---|---|
| Знімка немає або застарів | кадр не йде, одне попередження на зміну |
| Запис не вдався або приймача немає | одне попередження на зміну; ровер їде як зазвичай |
| Task WDT не підписано | журнал помилки, задача працює |

## Definition of Done

- [ ] збірка без нових попереджень; `check_delays.sh` чистий; пульт показує напругу — T12 (приймання на залізі: acceptance.md)
- [x] кожне Hard Rule вище лишається дійсним (інваріанти п. 7 конституції не зачеплено)
- [x] збірка без нових попереджень
