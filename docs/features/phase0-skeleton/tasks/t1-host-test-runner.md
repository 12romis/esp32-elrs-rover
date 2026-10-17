---
id: T1
title: "Створити хостовий раннер і скрипти перевірок test/host"
layer: "tests"
deps: []
blocks: ["T2", "T3", "T4"]
acs: ["AC-08", "AC-09", "AC-10", "AC-11"]
files_hint: ["test/host/"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "done"
step: "COMMIT"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T1 — Створити хостовий раннер і скрипти перевірок test/host

## Place in the sequence

- **Blocked by:** — · **Blocks:** T2 — Створити компонент app_state: таблиця переходів, запит переходу, історія (core), T3 — Створити console_core: збирання рядка, розбір команд, права за станом, T4 — Створити storage_core і watchdog_core: політика сховища та політика годування · **Wave:** 1.
- **Lane:** own lane.

## Why (user story)

> **As a** Агент  
> **I want** збирати проєкт і запускати хостові тести чистої логіки однією зрозумілою процедурою  
> **So that** я можу довести, що задача завершена, не маючи плати
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Дає Агентові єдину процедуру «зібрати й прогнати тести» та автоматичні перевірки заборонених заголовків, літералів пінів і захищених файлів.

## Inlined context

> Хостові тести збираються звичайним gcc (не `idf.py --preview set-target linux`); раннер друкує кількість тестів і результат; прогін ≤ 5 с, 100 % пройдено. Лінтера немає, єдина перевірка збірки — `idf.py build` без нових попереджень.
>
> — `spec.md §6, NFR «Хостовий прогін тестів» + sad.md §11, ризик «Хостового раннера ще немає», abridged` · full text: [spec.md](../spec.md)

> Хостові тести — без фреймворку: звичайний gcc + прості `assert`-тести; окрема задача створює раннер без нових залежностей.
>
> — `sad.md §11, ризик про хостовий раннер, abridged` · full text: [sad.md](../sad.md)

> **Hard rule:** файли `*_core.c` не підключають жодного заголовка ESP-IDF чи FreeRTOS (лише `stdint.h`, `stdbool.h`, `stddef.h`, `string.h`); вся логіка тут, її тести — на звичайному gcc у `test/host/`; для HAL моки не пишуться.
>
> — `constitution.md, п. 6 і AC-09, abridged` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** номери GPIO існують лише в `components/board/include/board_pins.h`; літерал номера лінії деінде — дефект. Фаза 0 не налаштовує жодної лінії мотора і не посилається на `PIN_STBY`.
>
> — `constitution.md, п. 2 і ADR-0003, abridged` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** `CMakeLists.txt` у корені, `.vscode/`, `sdkconfig.defaults` не змінюються без явної згоди Розробника; `kicad-rover/` Агент не редагує; Агент не комітить і не прошиває без прямої вказівки.
>
> — `constitution.md, п. 10–11 і CLAUDE.md, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** недостатньо або суперечить коду → прочитати названий файл повністю ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) і діяти за ним. Не вигадувати.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-08 (US-05) — happy path

> **Given** Агент завершив зміну в межах фази 0  
> **When** Агент запускає збірку проєкту і прогін хостових тестів  
> **Then** збірка завершується без помилок і без нових попереджень, а прогін повідомляє кількість тестів і що всі вони пройдені
>
> — `spec.md §5, AC-08, verbatim` · full text: [spec.md](../spec.md)

### AC-09 (US-05) — error

> **Given** у зміні з'явилося нове попередження збірки або файл чистої логіки (`*_core.c`) підключив будь-який заголовок ESP-IDF чи FreeRTOS  
> **When** Агент запускає збірку і хостовий прогін  
> **Then** задачу не вважають завершеною, Агент називає файл і причину та не позначає задачу виконаною
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

### AC-10 (US-06) — authorization

> **Given** Агент виконує задачу фази 0  
> **When** для задачі потрібна зміна захищеного файла або будь-яка правка в каталозі проєкту плати  
> **Then** Агент нічого не змінює, зупиняється, описує потрібну зміну та причину і чекає явної згоди Розробника; прошивання плати та фіксацію змін в історії Агент теж виконує лише за прямою вказівкою
>
> — `spec.md §5, AC-10, verbatim` · full text: [spec.md](../spec.md)

### AC-11 (US-06) — domain invariant

> **Given** кодова база фази 0  
> **When** Розробник або Агент перевіряє розпіновку  
> **Then** номери ліній плати присутні лише в єдиному файлі розпіновки, а будь-який інший файл, що містить літерал номера лінії, вважається дефектом, навіть якщо номер правильний
>
> — `spec.md §5, AC-11, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `test/host/test_support.h`: макрос `CHECK(cond)` з лічильниками, функції `test_summary()` — друкує «N тестів, M пройдено», код виходу ≠ 0 при помилці або при 0 тестів
- [ ] `test/host/Makefile` і `test/host/run_tests.sh`: збирає кожен `test_*.c` з відповідними `components/*/*_core.c` (`gcc -Wall -Wextra -Werror -std=c11`), запускає, підсумовує
- [ ] `test/host/check_core_headers.sh`: падає, якщо `components/*/*_core.c` містить `#include` з `esp_`, `freertos/`, `driver/`, `nvs`, `sdkconfig`, `soc/`; друкує файл і причину
- [ ] `test/host/check_pins.sh`: падає, якщо поза `components/board/include/board_pins.h` знайдено `GPIO_NUM_<n>`, `GPIOnn` або `PIN_*` з літералом; друкує файл
- [ ] `test/host/check_protected.sh`: падає, якщо `git diff --name-only` (робоче дерево) торкається `CMakeLists.txt` у корені, `.vscode/`, `sdkconfig.defaults` або `kicad-rover/`
- [ ] `test/host/README.md`: одна процедура запуску (`test/host/run_tests.sh`) і опис перевірок (українською)
- [ ] Перевірити власними негативними прикладами у тимчасовому каталозі (не комітяться), що кожен скрипт справді падає

## Edge cases

| Case | Behaviour |
|---|---|
| Жодного `test_*.c` ще немає | раннер повідомляє «0 тестів» і завершується з ненульовим кодом (не «все пройдено») |
| `*_core.c` підключив `freertos/FreeRTOS.h` | `check_core_headers.sh` називає файл і заголовок, код виходу ≠ 0 |
| Літерал піна у `components/foo/foo_hal.c` | `check_pins.sh` називає файл і рядок |
| Зміна у `sdkconfig.defaults` в робочому дереві | `check_protected.sh` називає файл; Агент не вважає задачу виконаною |

## Definition of Done

- [ ] `test/host/run_tests.sh` на порожньому наборі повертає ненульовий код; на наборі з тестом — «N тестів, N пройдено» за ≤ 5 с
- [ ] Три скрипти перевірок падають на навмисно поганих прикладах і мовчать на чистому дереві
- [ ] Жодних правок захищених файлів; `idf.py build` не зламано
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
