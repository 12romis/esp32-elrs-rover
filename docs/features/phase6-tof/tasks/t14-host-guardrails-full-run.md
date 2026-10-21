---
id: T14
title: "Посилити хостові перевірки: повний прогін, статичні перевірки core, пінів, затримок і калібрування для фази 6"
layer: "tests"
deps: ["T13"]
blocks: []
acs: ["AC-17", "AC-18"]
files_hint: ["test/host/run_tests.sh", "test/host/selftest_tools.sh", "test/host/README.md"]
owner: "Агент"
estimate: "S"
context_budget: "M"
status: "done"
step: "COMMIT"
---

# T14 — Посилити хостові перевірки: повний прогін, статичні перевірки core, пінів, затримок і калібрування для фази 6

## Place in the sequence

- **Blocked by:** T13 — Підключити в main старт tof і колбеки консолі tof_show та tof_reinit · **Blocks:** — · **Wave:** 7.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Агент
> **I want** покрити логіку далекомірів і протизіткнення хостовими тестами та зібрати прошивку без попереджень
> **So that** помилки фільтра, секторів і переходів ловляться без плати, а залізо перевіряє лише те, чого хост не бачить
>
> — `spec.md §4, US-06, verbatim` · full text: [spec.md](../spec.md)

Ця задача перетворює правила фази на перевірки, що ловлять заголовок ESP-IDF у core, літерал піна, звичайну затримку й невиміряний поріг.

## Inlined context

> **Hard rule:** 6. Кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`. Логіка живе в core і покривається тестами на хості. Для HAL тести на моках не пишуться: критерій приймання — перевірка на залізі, записана в журнал bring-up.
>
> — `constitution.md, п. 6, verbatim` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** 8. Будь-яка задача вважається завершеною лише якщо `idf.py build` проходить без помилок і без нових попереджень.
>
> — `constitution.md, п. 8, verbatim` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** 14. Шина I²C працює на 100 кГц, підвищувати до 400 кГц заборонено (розділ 4.6 документа `02-FIRMWARE-CONTEXT.md`). Усі періодичні задачі використовують `vTaskDelayUntil`, а не `vTaskDelay` (розділ 8.5).
>
> — `constitution.md, п. 14, verbatim` · full text: [constitution.md](../../../constitution.md)

> | Хостовий прогін тестів | ≤ 5 с, 100 % тестів пройдено | вивід хостового раннера |
>
> — `spec.md §6, NFR «Хостовий прогін тестів», verbatim` · full text: [spec.md](../spec.md)

> | Збірка | 0 помилок, 0 нових попереджень | вивід збірки проєкту |
>
> — `spec.md §6, NFR «Збірка», verbatim` · full text: [spec.md](../spec.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-17 (US-06) — happy path

> **Given** Агент завершив зміну в межах фази 6
> **When** Агент запускає збірку проєкту і прогін хостових тестів
> **Then** збірка завершується без помилок і без нових попереджень, а прогін повідомляє кількість тестів і що всі пройдені; серед них перевірено: медіану з трьох відліків (зокрема викид, відлік поза діапазоном, перші відліки після старту); відстань сектору за мінімумом справних далекомірів і сектор без жодного справного; зайнятий сектор строго за порогом і звільнення за трьома поспіль відліками; обмеження тяги знизу й зверху з незмінним поворотом, розворот на місці, два зайняті сектори; вибір відмови за 250 мс без відліку; перехід ARMED ↔ OBSTACLE_HOLD і відсутність переходу, коли захист вимкнено; таблицю переходів з 12 пар і відсутність входу в OBSTACLE_HOLD з інших станів; дозволи команд далекомірів за станом; розбір форм команди (показ, повторна ініціалізація, відхилення зайвого й спроби задати поріг); збереження інваріантів ARMED у OBSTACLE_HOLD (напруга, втрата звʼязку, нове зʼявлення пульта, спливання команди руху)
>
> — `spec.md §5, AC-17, verbatim` · full text: [spec.md](../spec.md)

### AC-18 (US-06) — error

> **Given** у зміні файл чистої логіки (з суфіксом core) підключив заголовок ESP-IDF чи FreeRTOS, або номер лінії зʼявився літералом поза єдиним файлом розпіновки, або поріг перешкоди заданий літералом поза файлом калібрування чи числом, якого немає в журналі вимірювань, або частота шини вища за 100 кГц, або періодична задача чекає звичайною затримкою замість очікування до моменту, або зʼявилося нове попередження збірки
> **When** Агент запускає збірку і хостові перевірки
> **Then** задачу не вважають завершеною, Агент називає файл і причину та не позначає задачу виконаною
>
> — `spec.md §5, AC-18, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Переконатися, що `run_tests.sh` збирає `test_tof.c` з усіма `components/*/*_core.c` — `test/host/run_tests.sh`
- [ ] Додати в `selftest_tools.sh` випадки: заголовок ESP-IDF у `tof_core.c`, літерал піна в `tof_hal.c`, `vTaskDelay` поза дозволеним винятком, `TOF_OBSTACLE_MM` ≠ рядка в журналі вимірювань — `test/host/selftest_tools.sh`
- [ ] Оновити опис тестів — `test/host/README.md`
- [ ] Запустити `test/host/run_tests.sh`, усі `check_*.sh` і `idf.py build`, зафіксувати 0 нових попереджень

## Edge cases

| Case | Behaviour |
|---|---|
| Файл core підключає заголовок ESP-IDF | `check_core_headers.sh` падає з назвою файлу |
| Номер піна літералом поза `board_pins.h` | `check_pins.sh` падає |
| `TOF_OBSTACLE_MM` відрізняється від `docs/measured.md` | `check_calib.sh` падає з обома значеннями |
| `vTaskDelay` у `tof_hal.c` поза очікуванням регістра | Вручну в рев'ю: виняток лише для `vTaskDelay(1)` у ініціалізації |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений, ≤ 5 с
- [ ] Усі `check_*.sh` чисті, `selftest_tools.sh` зелений
- [ ] `idf.py build` без помилок і нових попереджень
- [ ] every Hard Rule inlined above still holds
