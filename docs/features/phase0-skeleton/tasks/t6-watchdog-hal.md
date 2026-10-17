---
id: T6
title: "Створити watchdog_hal: програмний panic для TWDT, задача heartbeat, причина скидання"
layer: "infra"
deps: ["T4"]
blocks: ["T8"]
acs: ["AC-04"]
files_hint: ["components/watchdog/"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T6 — Створити watchdog_hal: програмний panic для TWDT, задача heartbeat, причина скидання

## Place in the sequence

- **Blocked by:** T4 — Створити storage_core і watchdog_core: політика сховища та політика годування · **Blocks:** T8 — Скласти main.c: послідовність старту, REQUIRES, видалення main/app_states · **Wave:** 3.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник  
> **I want** навмисно «зависнути» службову задачу командою і побачити самостійне перезавантаження з названою причиною  
> **So that** я знаю, що захист від зависання працює, до того, як на платі з'являться мотори
>
> — `spec.md §4, US-03, verbatim` · full text: [spec.md](../spec.md)

Вмикає panic-режим Task WDT програмно (без правки `sdkconfig.defaults`), стартує heartbeat і дає назву причини останнього скидання.

## Inlined context

> `esp_task_wdt_reconfigure()` з `trigger_panic = true`; таймаут і маска ядер із Kconfig-макросів (`CONFIG_ESP_TASK_WDT_TIMEOUT_S`, `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0/CPU1`), жодних літералів. Після спрацювання `esp_reset_reason()` = `ESP_RST_TASK_WDT`.
>
> — `adr/0001, Decision outcome + Context, abridged` · full text: [adr/0001](../adr/)

> Задача `heartbeat`: core 0, період 1000 мс через `vTaskDelayUntil`; годує Task WDT, доки `watchdog_should_feed`; після запиту зависання не годує, а зупиняє себе через `vTaskSuspend`.
>
> — `sad.md §4, журнал припущень + §7, abridged` · full text: [sad.md](../sad.md)

> Затримка перезавантаження після `wdt-test` ≤ 6 с (таймаут 5 с).
>
> — `spec.md §6, NFR «Затримка перезавантаження…», verbatim` · full text: [spec.md](../spec.md)

> **Hard rule:** `CMakeLists.txt` у корені, `.vscode/`, `sdkconfig.defaults` не змінюються без явної згоди Розробника; `kicad-rover/` Агент не редагує; Агент не комітить і не прошиває без прямої вказівки.
>
> — `constitution.md, п. 10–11 і CLAUDE.md, abridged` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** коментарі українською, ідентифікатори англійською; кожна задача завершена лише коли `idf.py build` без помилок і без нових попереджень.
>
> — `constitution.md, п. 8–9, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** недостатньо або суперечить коду → прочитати названий файл повністю ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) і діяти за ним. Не вигадувати.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-04 (US-03) — happy path

> **Given** автомат у стані DISARMED  
> **When** Розробник вводить команду навмисного зависання службової задачі (окрема фонова задача-«пульс», яку стежить сторожовий таймер; команда припиняє її годування, а консоль до перезавантаження лишається працездатною; очікування вводу в консолі саме по собі сторожовий таймер не спрацьовує)  
> **Then** консоль попереджає про очікуване перезавантаження, плата сама перезавантажується приблизно через 5 с (допуск ≤ 6 с, див. §6), а після старту і журнал, і команда показу стану називають причиною останнього скидання сторожовий таймер задач
>
> — `spec.md §5, AC-04, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `components/watchdog/watchdog_hal.c`: `watchdog_start()` — `esp_task_wdt_reconfigure({timeout_ms, idle_core_mask, trigger_panic=true})`, перевірка `esp_err_t`, створення задачі `heartbeat` (core 0, `esp_task_wdt_add`)
- [ ] Задача `heartbeat`: цикл `vTaskDelayUntil` 1000 мс; `watchdog_should_feed` → `esp_task_wdt_reset`, інакше `vTaskSuspend(NULL)`
- [ ] `watchdog_request_hang()` — викликає `watchdog_core_request_hang`; `watchdog_last_reset_reason()` — мапінг `esp_reset_reason()` у короткі назви (POWERON, SW, TASK_WDT, PANIC, BROWNOUT, інші)
- [ ] Оновити `components/watchdog/CMakeLists.txt`: `watchdog_hal.c`, `REQUIRES esp_system freertos`
- [ ] `idf.py build`: 0 помилок, 0 нових попереджень

## Edge cases

| Case | Behaviour |
|---|---|
| `esp_task_wdt_reconfigure` повертає помилку | `ESP_LOGE` з назвою помилки, `watchdog_start` повертає `esp_err_t`; без `abort()` |
| Очікування вводу в консолі | `console` не підписана на TWDT, тому очікування вводу не спрацьовує сторожовий таймер |
| Запит зависання | heartbeat припиняє годування, плата перезавантажується ≤ 6 с, причина `TASK_WDT` |

## Definition of Done

- [ ] `idf.py build` без помилок і без нових попереджень
- [ ] `sdkconfig.defaults` і `sdkconfig` не змінено
- [ ] Приймання на залізі (секундомір, причина скидання) — запис у журнал bring-up за Розробником
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
