---
id: T3
title: "Додати в app_state предикат «ровер їде» і пару OBSTACLE_HOLD → DISARMED (12 пар із 49)"
layer: "domain"
deps: []
blocks: ["T5", "T7", "T8"]
acs: ["AC-13", "AC-17"]
files_hint: ["components/app_state/app_state_core.c", "components/app_state/include/app_state.h", "test/host/test_app_state.c", "docs/02-FIRMWARE-CONTEXT.md"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T3 — Додати в app_state предикат «ровер їде» і пару OBSTACLE_HOLD → DISARMED (12 пар із 49)

## Place in the sequence

- **Blocked by:** — · **Blocks:** T5 — Вбудувати в drive_ctl_step знімок далекомірів: обмеження тяги після мікшера, запити ARMED ↔ OBSTACLE_HOLD, застарілий знімок, T7 — Навчити ui_core OBSTACLE_HOLD: задні ліхтарі авто, вхід у рух вмикає фари й сигнал, перехід між ARMED і OBSTACLE_HOLD їх не чіпає, T8 — Додати в console_core команду tof (показ і reinit), матрицю дозволів і прийом drive в OBSTACLE_HOLD · **Wave:** 1.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** щоб у OBSTACLE_HOLD діяли відсічка по напрузі, втрата звʼязку, роззброєння та вся решта правил ARMED, а світло й регулятор працювали як зазвичай
> **So that** новий стан не стає лазівкою в захисті ровера
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Ця задача дає єдиний предикат руху і шлях роззброєння з OBSTACLE_HOLD, без яких решта компонентів не може безпечно прийняти новий стан.

## Inlined context

> **Chosen:** варіант 1. `app_state_is_driving(state)` істинний для ARMED і OBSTACLE_HOLD; `drive_core`, `drive_hal` (журнал роззброєння, `last_stop`, лічильник затримки «кадр → борти»), `console_core`, `pid` (скидання, трасування, блокування команд) і `ui_core` використовують його там, де йдеться про рух.
>
> — `adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md, Decision outcome, abridged` · full text: [adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md](../adr/0004-treat-obstacle-hold-as-a-driving-state-through-one-predicate.md)

> - `ui_core`: режим задніх ліхтарів «авто» діє в ARMED, OBSTACLE_HOLD і FAILSAFE;
>
> — `sad.md §4, журнал припущень design, bullet «ui_core», abridged` · full text: [sad.md](../sad.md)

> - Документ 02 §6.3 не має виходу OBSTACLE_HOLD → DISARMED, а глосарій і фази 1–3 кажуть, що роззброїти ровер можна завжди. Рішення власника: додати пару до таблиці (12 пар із 49); тест «рівно 11 пар» і §6.3 документа 02 оновлюються разом із кодом.
>
> — `spec.md §1 ¶4, «Розбіжності», перший пункт, verbatim` · full text: [spec.md](../spec.md)

> **Hard rule:** 7. Інваріанти безпеки, сильніші за будь-яку функціональність:    - STBY піднімається тільки в стані ARMED і тільки свідомо;    - втрата кадрів CRSF довша за 500 мс → ШІМ у нуль, STBY вниз;    - перша операція по I²C після старту — запис `0x00` у PCF8574;    - VBAT < 6.0 В → мотори не запускаються.
>
> — `constitution.md, п. 7, verbatim` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-13 (US-05) — domain invariant

> **Given** автомат станів із таблицею переходів
> **When** перевіряють кожну пару з семи станів
> **Then** у таблиці рівно 12 дозволених пар із 49: пари фаз 0–3 плюс OBSTACLE_HOLD → DISARMED; в OBSTACLE_HOLD можна потрапити лише з ARMED, а вийти — в ARMED, DISARMED і FAILSAFE; з BOOT, SELFTEST, DISARMED, FAILSAFE і FAULT переходу в OBSTACLE_HOLD немає; команда озброєння в OBSTACLE_HOLD відхиляється, як в ARMED; STBY піднімається лише при вході в ARMED з DISARMED і не піднімається жодним переходом, що веде в OBSTACLE_HOLD чи з нього
>
> — `spec.md §5, AC-13, verbatim` · full text: [spec.md](../spec.md)

### AC-17 (US-06) — happy path

> **Given** Агент завершив зміну в межах фази 6
> **When** Агент запускає збірку проєкту і прогін хостових тестів
> **Then** збірка завершується без помилок і без нових попереджень, а прогін повідомляє кількість тестів і що всі пройдені; серед них перевірено: медіану з трьох відліків (зокрема викид, відлік поза діапазоном, перші відліки після старту); відстань сектору за мінімумом справних далекомірів і сектор без жодного справного; зайнятий сектор строго за порогом і звільнення за трьома поспіль відліками; обмеження тяги знизу й зверху з незмінним поворотом, розворот на місці, два зайняті сектори; вибір відмови за 250 мс без відліку; перехід ARMED ↔ OBSTACLE_HOLD і відсутність переходу, коли захист вимкнено; таблицю переходів з 12 пар і відсутність входу в OBSTACLE_HOLD з інших станів; дозволи команд далекомірів за станом; розбір форм команди (показ, повторна ініціалізація, відхилення зайвого й спроби задати поріг); збереження інваріантів ARMED у OBSTACLE_HOLD (напруга, втрата звʼязку, нове зʼявлення пульта, спливання команди руху)
>
> — `spec.md §5, AC-17, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Додати `bool app_state_is_driving(app_state_t s)` (ARMED або OBSTACLE_HOLD) і оголосити — `components/app_state/app_state_core.c`, `components/app_state/include/app_state.h`
- [ ] Додати в таблицю пару `[OBSTACLE_HOLD][DISARMED]`; оновити коментар: 12 пар із 49 — `components/app_state/app_state_core.c`
- [ ] Оновити `test/host/test_app_state.c`: «рівно 12 пар», пара HOLD→DISARMED дозволена (рядок 115 тепер навпаки), вхід в OBSTACLE_HOLD лише з ARMED, предикат для всіх семи станів — `test/host/test_app_state.c`
- [ ] Оновити діаграму автомата у §6.3 — `docs/02-FIRMWARE-CONTEXT.md`

## Edge cases

| Case | Behaviour |
|---|---|
| Запит OBSTACLE_HOLD з DISARMED, BOOT, SELFTEST, FAILSAFE, FAULT | Відхилено, стан не змінюється |
| Запит OBSTACLE_HOLD → DISARMED | Дозволено одним переходом |
| Запит OBSTACLE_HOLD → ARMED та → FAILSAFE | Дозволено, як раніше |
| Запит у той самий стан | Відхилено, як раніше |
| Предикат для FAILSAFE, DISARMED, BOOT, SELFTEST, FAULT | Хибний; істинний лише для ARMED і OBSTACLE_HOLD |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений: таблиця має рівно 12 пар із 49
- [ ] `check_core_headers.sh` чистий
- [ ] `idf.py build` без помилок і нових попереджень
- [ ] Коментарі й документ 02 §6.3 кажуть 12 пар
- [ ] every Hard Rule inlined above still holds
