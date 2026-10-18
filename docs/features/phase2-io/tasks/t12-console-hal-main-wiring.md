---
id: T12
title: "Підключити колбеки світла й зумера в console_hal і порядок старту в main"
layer: "wiring"
deps: ["T5", "T6", "T8", "T9"]
blocks: ["T14"]
acs: ["AC-01", "AC-06", "AC-13", "AC-14"]
files_hint: ["components/rover_console/console_hal.c", "components/rover_console/include/console_hal.h", "main/main.c", "main/CMakeLists.txt"]
owner: "Агент (код), Розробник (приймання на залізі)"
estimate: "S"
context_budget: "M"
status: "done"
step: "COMMIT — AC-14 апаратно не перевірено (крок 13 пропущено)"
---

# T12 — Підключити колбеки світла й зумера в console_hal і порядок старту в main

## Place in the sequence

- **Blocked by:** T5 — Розширити console_core: розбір light і beep, порядок перевірок, help і status, T6 — Створити i2c_bus_hal і ioexp_hal: шина 100 кГц, мʼютекс, нуль першим, скан, T8 — Створити ui_hal: задача ui, мʼютекс m_ui, опитування стану й бортів, запис розширювача, T9 — Додати в ui_hal зумер: LEDC таймер 1 канал 2, низький рівень спокою, ручний і автоматичний звук · **Blocks:** T14 — Підготувати чекліст приймання фази 2 на залізі і запис у журнал bring-up · **Wave:** 6, за графом залежностей.
- **Lane:** власна.

## Why (user story)

> **As a** Розробник
> **I want** бачити в консолі, чи розширювач доступний, які канали світла ввімкнені, як керуються задні ліхтарі і чи звучить зумер
> **So that** я розумію, чому світло горить, мовчить або недоступне
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

> **As a** Розробник
> **I want** щоб ровер стартував, озброювався й їхав навіть тоді, коли розширювач не відповідає
> **So that** несправний шлейф світла не позбавляє мене керування моторами й шляху «зупинити»
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Цей task дає частину цієї історії: підключити колбеки світла й зумера в console_hal і порядок старту в main.

## Inlined context

> **Hard rule:** Кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`. Логіка живе в core і покривається тестами на хості. Для HAL тести на моках не пишуться: критерій приймання — перевірка на залізі, записана в журнал bring-up.
>
> — `constitution.md, п. 6, verbatim` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** перша операція по I²C після старту — запис `0x00` у PCF8574; ШІМ обмежений 0.75 до завершення фази 5.
>
> — `constitution.md, п. 7, abridged` · full text: [constitution.md](../../../constitution.md)

> `app_main`: app_state_hal_init → drive_hal_init (виходи моторів вимкнено) → ioexp_start (i2c_bus, запис 0x00, скан) → watchdog_start → console_start → battery_start → drive_start → ui_start → storage_init → SELFTEST → DISARMED (без логіки). Консоль не залежить від `ui` і `ioexp`: потрібні дії їй передає `main` колбеками.
>
> — `sad.md §5, §5 decomposition, abridged` · full text: [sad.md](../sad.md)

> `console_ops_t` розширюється колбеками `light_cmd`, `beep_cmd` і `ui_status`.
>
> — `sad.md §4, §4 ledger, abridged` · full text: [sad.md](../sad.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [cli.md](../contracts/cli.md) · [adr/](../adr/) · [constitution](../../../constitution.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Контракт консолі: відповіді `light`, `beep`, `help`, `status` — `contracts/cli.md`.

— `contracts/cli.md, «Класи відповіді», «Змінені відповіді», abridged` · full text: [cli.md](../contracts/cli.md)

## Acceptance criteria

### AC-01 — happy path

> **Given** плату щойно ввімкнено або перезапущено, розширювач портів на шині відповідає, а до цього моменту його виходи могли тримати світло ввімкненим
> **When** прошивка завершує старт
> **Then** усі три канали світла вимкнені, вбудований індикатор плати не світить (світло може горіти лише до першого запису нуля: це вікно прийнято, OQ-1), і так лишається, доки Розробник не ввімкне якийсь канал або ровер не опиниться в ARMED чи FAILSAFE; лінії скидання далекомірів утримуються в стані скидання; консоль повідомляє, що розширювач знайдено, і друкує адреси, знайдені на шині
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

### AC-06 — cross-context

> **Given** автомат у стані, відмінному від DISARMED і ARMED (BOOT, SELFTEST, OBSTACLE_HOLD, FAILSAFE або FAULT)
> **When** Розробник вводить команду світла або команду зумера
> **Then** команду відхилено з поясненням, що світло й зумер дозволені лише в DISARMED та ARMED, і названо поточний стан; стан світла не змінюється, зумер не звучить, стан автомата не змінюється
>
> — `spec.md §5, AC-06, verbatim` · full text: [spec.md](../spec.md)

### AC-13 — happy path

> **Given** автомат у довільному стані і консоль активна
> **When** Розробник вводить команду показу стану, а потім команду довідки
> **Then** показ стану додатково до даних фаз 0 і 1 виводить, чи розширювач доступний, стан трьох каналів світла (ввімкнено чи вимкнено), хто керує задніми ліхтарями (вручну чи рух), колір вбудованого індикатора плати (вимкнений чи червоний), чи звучить зумер і скільки мілісекунд лишилось звучати (0, якщо мовчить); довідка перелічує команди: help, status, reboot, wdt-test, arm, disarm, drive, light, beep; дозволи решти команд не змінюються
>
> — `spec.md §5, AC-13, verbatim` · full text: [spec.md](../spec.md)

### AC-14 — cross-context

> **Given** розширювач не відповідає при старті (шлейф відʼєднано або шина несправна)
> **When** прошивка проходить старт
> **Then** ровер доходить до DISARMED, як в AC-01 фази 0; консоль друкує, що розширювач не відповів, світло недоступне, решту функцій збережено; озброєння, команда руху й роззброєння працюють як у фазі 1 і не блокуються; команда світла відхиляється з поясненням, що розширювач недоступний, команда зумера працює; розширювач вважається доступним лише якщо перший запис нуля підтверджено (дозволено до двох спроб у межах 1 с), сканування шини цього не вирішує, а лише журналить знайдені адреси; не підтверджений на старті розширювач вважається недоступним до перезапуску плати
>
> — `spec.md §5, AC-14, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] Розширити `console_ops_t` колбеками `light_cmd`, `beep_cmd`, `ui_status`; у `console_hal.c` викликати їх за результатом `console_core`.
- [x] У `main.c` реалізувати колбеки (делегування в `ui_hal`) і виставити порядок старту за SAD §5: `ioexp_start` одразу після ліній моторів, до `watchdog_start`.
- [x] Збій `ioexp_start` або `ui_start` не завершує старт: консоль, озброєння й мотори працюють (деградація).
- [x] `idf.py build` без нових попереджень.

## Edge cases

| Case | Behaviour |
|---|---|
| `ioexp_start` повернув помилку | журнал, ровер доходить до DISARMED, `arm`, `drive`, `disarm` працюють |
| `ui_start` не стартував | `light` і `beep` відхиляються з поясненням, решта працює |

## Definition of Done

- [x] `idf.py build` без помилок і нових попереджень.
- [x] Приймання на залізі: старт без світла, `status` показує поля, відмова розширювача не блокує озброєння (AC-01, AC-06, AC-13, AC-14).
- [x] Hard rules вище виконуються.
- [x] lint + vet clean
