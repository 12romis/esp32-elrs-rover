---
id: T7
title: "Навчити ui_core OBSTACLE_HOLD: задні ліхтарі авто, вхід у рух вмикає фари й сигнал, перехід між ARMED і OBSTACLE_HOLD їх не чіпає"
layer: "app"
deps: ["T3"]
blocks: []
acs: ["AC-14", "AC-16"]
files_hint: ["components/ui/ui_core.c", "components/ui/include/ui_core.h", "test/host/test_ui.c"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T7 — Навчити ui_core OBSTACLE_HOLD: задні ліхтарі авто, вхід у рух вмикає фари й сигнал, перехід між ARMED і OBSTACLE_HOLD їх не чіпає

## Place in the sequence

- **Blocked by:** T3 — Додати в app_state предикат «ровер їде» і пару OBSTACLE_HOLD → DISARMED (12 пар із 49) · **Blocks:** — · **Wave:** 2.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** щоб у OBSTACLE_HOLD діяли відсічка по напрузі, втрата звʼязку, роззброєння та вся решта правил ARMED, а світло й регулятор працювали як зазвичай
> **So that** новий стан не стає лазівкою в захисті ровера
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Ця задача робить так, що в OBSTACLE_HOLD задні ліхтарі показують рух, а часті переходи біля перешкоди не блимають фарами й не пищать.

## Inlined context

> - `ui_core`: режим задніх ліхтарів «авто» діє в ARMED, OBSTACLE_HOLD і FAILSAFE; правило «вхід у рух» спирається на стан, який `ui` справді бачить (опитування раз на 100 мс): перехід зі стану не-руху (DISARMED тощо) в ARMED або OBSTACLE_HOLD вмикає фари й дає сигнал переходу (озброєння біля вже зайнятого сектору виглядає для `ui` як DISARMED → OBSTACLE_HOLD), а перехід між ARMED і OBSTACLE_HOLD їх не чіпає; ручні світло й зумер в OBSTACLE_HOLD, як і раніше, відхиляються.
>
> — `sad.md §4, журнал припущень design, bullet «ui_core», verbatim` · full text: [sad.md](../sad.md)

> **Chosen:** варіант 1. `app_state_is_driving(state)` істинний для ARMED і OBSTACLE_HOLD; `drive_core`, `drive_hal` (журнал роззброєння, `last_stop`, лічильник затримки «кадр → борти»), `console_core`, `pid` (скидання, трасування, блокування команд) і `ui_core` використовують його там, де йдеться про рух. Прямі порівняння з ARMED лишаються лише для озброєння (вхід з DISARMED, фіксація режиму регулятора, фари при вході). Перехід ARMED ↔ OBSTACLE_HOLD не скидає регулятор, не вмикає фари примусово й не дає сигналу переходу;
>
> — `adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md, Decision outcome, abridged` · full text: [adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md](../adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md)

> - A12. Окремої індикації перешкоди немає: у OBSTACLE_HOLD задні ліхтарі показують рух як в ARMED, зумер мовчить, а перехід між ARMED і OBSTACLE_HOLD не вмикає фари примусово й не дає сигналу переходу; інформація — у журналі консолі.
>
> — `spec.md §1 ¶3, A12, verbatim` · full text: [spec.md](../spec.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-14 (US-05) — cross-context

> **Given** ровер у OBSTACLE_HOLD (передній сектор зайнятий), регулятор швидкості увімкнено, трасування регулятора ввімкнене, пульт керує
> **When** Розробник їде назад стіком, а потім намагається зберегти набір коефіцієнтів регулятора
> **Then** регулятор тримає задану швидкість назад із допуском фази 5 (±5 % після розгону); рядки трасування виводяться раз на 100 мс, як в ARMED (зміна відносно фази 5, де рядки йшли лише в ARMED); задні ліхтарі показують напрямок руху, як в ARMED; фари лишаються такими, якими були (перехід між ARMED і OBSTACLE_HOLD не вмикає їх примусово), і сигнал переходу не звучить; збереження набору відхилено так само, як в ARMED, з поясненням, що ровер озброєний
>
> — `spec.md §5, AC-14, verbatim` · full text: [spec.md](../spec.md)

### AC-16 (US-05) — cross-context

> **Given** ровер озброєний, фари й задні ліхтарі працюють, далекоміри опитуються
> **When** Розробник їде 60 с, кілька разів зупиняючись перед перешкодою, і робить кілька повторних ініціалізацій у DISARMED
> **Then** світло не блимає без причини, у журналі немає жодного попередження про помилку шини чи розширювача портів, а затримка «кадр пульта → борти» не перевищує межу §6; шина працює на 100 кГц
>
> — `spec.md §5, AC-16, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `ui_core_rear_mode`: UI_REAR_AUTO для ARMED, OBSTACLE_HOLD, FAILSAFE — `components/ui/ui_core.c`
- [ ] У `ui_core_tick` фари вмикаються і сигнал переходу звучить, коли попередній стан, який бачить `ui`, не руху, а новий — ARMED або OBSTACLE_HOLD; перехід між ARMED і OBSTACLE_HOLD не чіпає ні фар, ні сигналу — `components/ui/ui_core.c`
- [ ] Ручні світло й зумер в OBSTACLE_HOLD лишаються відхиленими — `components/ui/ui_core.c`
- [ ] Тести за «Edge cases» — `test/host/test_ui.c`

## Edge cases

| Case | Behaviour |
|---|---|
| ARMED → OBSTACLE_HOLD → ARMED | Фари не змінюються, сигнал переходу не звучить |
| DISARMED → ARMED (озброєння) | Фари вмикаються, сигнал звучить, як у фазі 2 |
| DISARMED → OBSTACLE_HOLD (озброєння біля зайнятого сектору, `ui` пропустив ARMED) | Фари й сигнал, як при озброєнні |
| OBSTACLE_HOLD, їде назад | Задній ліхтар за знаками бортів, як в ARMED |
| Ручна команда світла в OBSTACLE_HOLD | Відхилено, як раніше (фаза 2) |
| OBSTACLE_HOLD → FAILSAFE | Сигнал і індикація FAILSAFE, як з ARMED |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений, тести фази 2 не зламано
- [ ] `check_core_headers.sh` чистий
- [ ] `idf.py build` без помилок і нових попереджень
- [ ] every Hard Rule inlined above still holds
