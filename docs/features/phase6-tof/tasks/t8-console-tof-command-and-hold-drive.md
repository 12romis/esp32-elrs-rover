---
id: T8
title: "Додати в console_core команду tof (показ і reinit), матрицю дозволів і прийом drive в OBSTACLE_HOLD"
layer: "ports"
deps: ["T3"]
blocks: ["T13"]
acs: ["AC-09", "AC-10", "AC-11", "AC-15"]
files_hint: ["components/rover_console/console_core.c", "components/rover_console/include/console_core.h", "test/host/test_console.c"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "todo"
step: ""
---

# T8 — Додати в console_core команду tof (показ і reinit), матрицю дозволів і прийом drive в OBSTACLE_HOLD

## Place in the sequence

- **Blocked by:** T3 — Додати в app_state предикат «ровер їде» і пару OBSTACLE_HOLD → DISARMED (12 пар із 49) · **Blocks:** T13 — Підключити в main старт tof і колбеки консолі tof_show та tof_reinit · **Wave:** 2.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** командою консолі бачити стан і відстань кожного далекоміра, відстань секторів, поріг і чи діє захист
> **So that** я перевіряю датчики на шасі й підбираю поріг за заміром
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Ця задача дає Розробнику команди бачити далекоміри, повертати їх у роботу в DISARMED і керувати стендовою командою руху й в OBSTACLE_HOLD.

## Inlined context

> - Консоль: `CONSOLE_CMD_TOF` у `console_core` (форми `tof` і `tof reinit`; будь-яка інша — відмова з переліком форм і поясненням, що поріг не змінюється); матриця дозволів зі spec §5; команда руху `drive` приймається в ARMED і OBSTACLE_HOLD; колбеки `tof_show`, `tof_reinit` передає `main`.
>
> — `sad.md §4, журнал припущень design, bullet «Консоль», verbatim` · full text: [sad.md](../sad.md)

> - **Повторна ініціалізація.** Лише DISARMED; торкається ліній скидання лише відмовивших далекомірів; зберігає стан світла й адреси справних.
>
> — `spec.md §5, «Поведінка захисту», Повторна ініціалізація, verbatim` · full text: [spec.md](../spec.md)

> | Команда | BOOT | SELFTEST | DISARMED | ARMED | OBSTACLE_HOLD | FAILSAFE | FAULT |
> |---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
> | tof (показ) | так | так | так | так | так | так | так |
> | tof reinit (повторна ініціалізація) | ні | ні | так | ні | ні | ні | ні |
>
> — `spec.md §5, «Матриця дозволів консольних команд за станом», verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. `app_state_is_driving(state)` істинний для ARMED і OBSTACLE_HOLD; `drive_core`, `drive_hal` (журнал роззброєння, `last_stop`, лічильник затримки «кадр → борти»), `console_core`, `pid` (скидання, трасування, блокування команд) і `ui_core` використовують його там, де йдеться про рух.
>
> — `adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md, Decision outcome, abridged` · full text: [adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md](../adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no network API. Консольна поверхня: `tof` (показ, усі стани), `tof reinit` (лише DISARMED); `drive` приймається в ARMED і OBSTACLE_HOLD. Матриця дозволів і відповіді — в «Inlined context» вище.

## Acceptance criteria

### AC-09 (US-03) — authorization

> **Given** ровер у будь-якому стані, крім DISARMED (BOOT, SELFTEST, ARMED, OBSTACLE_HOLD, FAILSAFE, FAULT)
> **When** Розробник подає команду повторної ініціалізації далекомірів
> **Then** команду відхилено з поясненням, що перезапуск далекомірів можливий лише в DISARMED, бо торкається ліній скидання на розширювачі портів; жоден далекомір, стан автомата, світло й команди бортам не змінюються
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

### AC-10 (US-04) — happy path

> **Given** ровер у будь-якому стані автомата
> **When** Розробник подає команду показу далекомірів
> **Then** консоль друкує для кожного з чотирьох далекомірів його місце (ліворуч-вперед, вперед, праворуч-вперед, назад), стан (працює, поза діапазоном, відмова) і відфільтровану відстань у міліметрах; для кожного сектору — відстань, і чи він зайнятий, вільний чи не охороняється; поріг перешкоди в міліметрах; чи діє захист; стан автомата, озброєння й команди бортам не змінюються
>
> — `spec.md §5, AC-10, verbatim` · full text: [spec.md](../spec.md)

### AC-11 (US-04) — error

> **Given** ровер у будь-якому стані автомата
> **When** Розробник подає команду далекомірів з невідомою формою, зайвими словами, нечисловим чи числовим аргументом (спроба задати поріг), або з комою замість крапки
> **Then** команду відхилено з поясненням, які форми допустимі (показ і повторна ініціалізація) і що поріг з консолі не змінюється; поріг, стан далекомірів, автомат і команди бортам не змінюються
>
> — `spec.md §5, AC-11, verbatim` · full text: [spec.md](../spec.md)

### AC-15 (US-05) — cross-context

> **Given** стендовий режим (пульта немає), ровер озброєний з консолі, передній сектор зайнятий
> **When** Розробник подає команду руху вперед, а потім команду руху назад
> **Then** на команду вперед обидва борти лишаються з нулем, консоль один раз пояснює, що напрямок заблоковано перешкодою (а не на кожен повтор); на команду назад ровер їде, команда діє 2 с і сама гасне, як у фазі 1; стендове керування захищене тим самим правилом, що й пульт
>
> — `spec.md §5, AC-15, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Додати `CONSOLE_CMD_TOF` і розбір форм `tof` та `tof reinit`; інша форма (зайві слова, число, кома) — відмова з переліком допустимих і поясненням «поріг з консолі не змінюється» — `components/rover_console/console_core.c`, `components/rover_console/include/console_core.h`
- [ ] Матриця: `tof` у всіх семи станах, `tof reinit` лише DISARMED (відмова з поясненням) — `components/rover_console/console_core.c`
- [ ] Команда `drive`: дозвіл `app_state_is_driving(state)` замість `state == ARMED` — `components/rover_console/console_core.c`
- [ ] Додати в `console_ops_t` колбеки `tof_show` і `tof_reinit` (консоль від tof не залежить) — `components/rover_console/include/console_core.h`
- [ ] Тести: 2 форми × 7 станів, хибні форми, `drive` в OBSTACLE_HOLD — `test/host/test_console.c`

## Edge cases

| Case | Behaviour |
|---|---|
| `tof` у будь-якому стані | Дозволено, нічого не змінює |
| `tof reinit` у BOOT, SELFTEST, ARMED, OBSTACLE_HOLD, FAILSAFE, FAULT | Відхилено з поясненням «лише DISARMED» |
| `tof 150`, `tof thr 150`, `tof reinit now`, `tof 1,5` | Відхилено, перелік форм, «поріг з консолі не змінюється» |
| `drive` в OBSTACLE_HOLD | Приймається (як в ARMED) |
| `drive` у DISARMED, BOOT, FAILSAFE | Відхилено, як раніше |
| Регістр і зайві пробіли в `TOF  reinit` | Розбір як для решти команд консолі |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений, матриця 2×7 і хибні форми перевірені
- [ ] `check_core_headers.sh` чистий
- [ ] `idf.py build` без помилок і нових попереджень
- [ ] Поріг з консолі не змінюється жодною формою
- [ ] every Hard Rule inlined above still holds
