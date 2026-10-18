---
id: T11
title: "Прогнати хостовий раннер і перевірки конституції для phase3-crsf, закрити покриття 49 пар і 126 пар"
layer: "tests"
deps: ["T6", "T8", "T10"]
blocks: ["T12"]
acs: ["AC-18", "AC-19"]
files_hint: ["test/host/run_tests.sh", "test/host/check_core_headers.sh", "test/host/check_pins.sh", "test/host/test_drive.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T11 — Прогнати хостовий раннер і перевірки конституції для phase3-crsf, закрити покриття 49 пар і 126 пар

## Place in the sequence

- **Blocked by:** T6 — Розширити drive_ctl_step: вихід з FAILSAFE після ≥ 1 с стабільного зв'язку з тумблером у DISARM і відсічка у FAILSAFE, T8 — Перевірити ui_core на виході FAILSAFE → DISARMED: блимання, писк і індикатор гаснуть, сигнал переходу звучить, T10 — Оновити консоль: help для arm і drive, відповіді disarm, рядки status про пульт через колбек rc_status · **Blocks:** T12 — Підготувати чекліст приймання фази 3 на залізі і місця для записів у журнал bring-up · **Wave:** 7.
- **Lane:** спільні файли з T4, T5, T6 — серіалізовано.

## Why (user story)

> **As a** Агент
> **I want** доводити розбір кадрів, перетворення каналів, умови озброєння, таймер зв'язку, вихід із FAILSAFE і дозволи консолі тестами на хості
> **So that** логіка перевірена до того, як Розробник озброїть ровер з пульта
>
> — `spec.md §4, US-08, verbatim` · full text: [spec.md](../spec.md)

Підтверджує, що вся чиста логіка фази доведена на хості, а правила конституції не порушені.

## Inlined context

> | Покриття дозволів | 49 з 49 пар станів автомата; 126 з 126 пар «команда × стан × режим керування» (9 команд × 7 станів × 2 режими: стенд і пульт) |
> | Хостовий прогін тестів | ≤ 5 с, 100 % тестів пройдено |
> | Збірка | 0 помилок, 0 нових попереджень |
> | Періодичні задачі | 0 викликів звичайної затримки в періодичних задачах: лише очікування до моменту (пункт 14 конституції) |
>
> — `spec.md §6, abridged` · full text: [spec.md](../spec.md)

> Хостові тести: `test/host/run_tests.sh` збирає кожен `test_*.c` з усіма `components/*/*_core.c`; `check_core_headers.sh` ловить заголовки ESP-IDF у core.
>
> — `sad.md §2, Conventions, verbatim` · full text: [sad.md](../sad.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-18 — happy path

> **Given** Агент завершив зміну в межах фази 3
> **When** Агент запускає збірку проєкту і прогін хостових тестів
> **Then** збірка завершується без помилок і без нових попереджень, а прогін повідомляє кількість тестів і що всі пройдені; серед них перевірено: збирання кадрів із потоку байтів (зокрема кадр, розрізаний між порціями, і пошук початку кадру після шуму), контрольну суму, розпакування 16 каналів на еталонному кадрі, обмеження діапазону, мертву зону й безперервність за її межею, умови й фронт озброєння, таймер зв'язку з підставним годинником (лише кадр каналів його скидає), вихід із FAILSAFE за причиною й часом стабільного зв'язку, закінчення стендового режиму, таблицю переходів автомата (11 дозволених пар із 49) і матрицю дозволів консольних команд у всіх станах і обох режимах керування
>
> — `spec.md §5, AC-18, verbatim` · full text: [spec.md](../spec.md)

### AC-19 — error

> **Given** у зміні файл чистої логіки (з суфіксом core) підключив заголовок ESP-IDF чи FreeRTOS, або номер лінії приймача з'явився літералом поза єдиним файлом розпіновки, або періодична задача чекає звичайною затримкою замість очікування до моменту, або з'явилося нове попередження збірки
> **When** Агент запускає збірку і хостові перевірки
> **Then** задачу не вважають завершеною, Агент називає файл і причину та не позначає задачу виконаною
>
> — `spec.md §5, AC-19, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Тест матриці 126 пар у `test/host/test_drive.c` (якщо не закрито в T4/T5/T10): 9 команд × 7 станів × 2 режими.
- [ ] Прогнати `run_tests.sh`, `check_core_headers.sh`, `check_pins.sh`, `check_protected.sh`; додати перевірку відсутності `vTaskDelay(` у новій задачі `crsf_rx` (crsf_hal.c), якщо скрипт її не ловить.
- [ ] Прогнати `idf.py build`, порівняти кількість попереджень із фазою 2.

## Edge cases

| Case | Behaviour |
|---|---|
| Новий core-файл підключив ESP-IDF заголовок | перевірка червона, задачу не закрито (AC-19) |

## Definition of Done

- [ ] Усі хостові тести зелені за ≤ 5 с
- [ ] Усі `check_*.sh` чисті
- [ ] `idf.py build` 0 помилок, 0 нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
