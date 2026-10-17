---
id: T4
title: "Створити storage_core і watchdog_core: політика сховища та політика годування"
layer: "domain"
deps: ["T1"]
blocks: ["T5", "T6"]
acs: ["AC-12", "AC-04"]
files_hint: ["components/storage/", "components/watchdog/", "test/host/test_storage.c", "test/host/test_watchdog.c"]
owner: "Агент"
estimate: "S"
context_budget: "M"
status: "done"
step: "COMMIT"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T4 — Створити storage_core і watchdog_core: політика сховища та політика годування

## Place in the sequence

- **Blocked by:** T1 — Створити хостовий раннер і скрипти перевірок test/host · **Blocks:** T5 — Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS, T6 — Створити watchdog_hal: програмний panic для TWDT, задача heartbeat, причина скидання · **Wave:** 2.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник  
> **I want** щоб прошивка пережила пошкоджене сховище налаштувань на платі  
> **So that** старий вміст після невдалого прошивання не блокує старт
>
> — `spec.md §4, US-07, verbatim` · full text: [spec.md](../spec.md)

Дає чисті рішення «очищати сховище чи ні» та «годувати сторожовий таймер чи ні», щоб HAL лише викликав драйвери.

## Inlined context

> Очищення сховища лише за двох результатів ініціалізації: «немає вільних сторінок» і «знайдено новішу версію». Будь-яка інша помилка (і невдача після очищення) не очищає і є невдалим стартом: автомат лишається в BOOT, `status` показує «сховище недоступне» й причину.
>
> — `spec.md §5, AC-12 + adr/0004, Decision outcome, abridged` · full text: [spec.md](../spec.md)

> `storage_core.c`: рішення «ініціалізувати / очистити / відмовити» і стан сховища (ok / очищено / недоступне з причиною) для `status`. `watchdog_core.c`: годувати, доки не запитано зависання (`wdt-test`).
>
> — `sad.md §5, Internal decomposition, abridged` · full text: [sad.md](../sad.md)

> Heartbeat годує Task WDT; після запиту зависання політика core каже не годувати.
>
> — `sad.md §6, «Critical flow 2», abridged` · full text: [sad.md](../sad.md)

> **Hard rule:** файли `*_core.c` не підключають жодного заголовка ESP-IDF чи FreeRTOS (лише `stdint.h`, `stdbool.h`, `stddef.h`, `string.h`); вся логіка тут, її тести — на звичайному gcc у `test/host/`; для HAL моки не пишуться.
>
> — `constitution.md, п. 6 і AC-09, abridged` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** коментарі українською, ідентифікатори англійською; кожна задача завершена лише коли `idf.py build` без помилок і без нових попереджень.
>
> — `constitution.md, п. 8–9, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** недостатньо або суперечить коду → прочитати названий файл повністю ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) і діяти за ним. Не вигадувати.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-12 (US-07) — cross-context

> **Given** ініціалізація сховища налаштувань на платі повідомляє «немає вільних сторінок» або «знайдено новішу версію»  
> **When** плата стартує  
> **Then** прошивка очищає сховище, повідомляє про це в консолі та продовжує старт до DISARMED; за непошкодженого сховища старт іде без очищення; будь-яка інша помилка ініціалізації сховища сховище не очищає й обробляється як невдалий старт (§8, OQ-4)
>
> — `spec.md §5, AC-12, verbatim` · full text: [spec.md](../spec.md)

### AC-04 (US-03) — happy path

> **Given** автомат у стані DISARMED  
> **When** Розробник вводить команду навмисного зависання службової задачі (окрема фонова задача-«пульс», яку стежить сторожовий таймер; команда припиняє її годування, а консоль до перезавантаження лишається працездатною; очікування вводу в консолі саме по собі сторожовий таймер не спрацьовує)  
> **Then** консоль попереджає про очікуване перезавантаження, плата сама перезавантажується приблизно через 5 с (допуск ≤ 6 с, див. §6), а після старту і журнал, і команда показу стану називають причиною останнього скидання сторожовий таймер задач
>
> — `spec.md §5, AC-04, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `components/storage/CMakeLists.txt` + `include/storage.h`: `storage_result_t {OK, NO_FREE_PAGES, NEW_VERSION, OTHER}`, `storage_action_t {USE, ERASE_AND_RETRY, FAIL}`, `storage_status_t {OK, WIPED, UNAVAILABLE}` і функції рішення
- [ ] `components/storage/storage_core.c`: `storage_decide(result)`, `storage_final_status(first, retry)`, `storage_status_text(status)`
- [ ] `components/watchdog/CMakeLists.txt` + `include/watchdog.h`: `watchdog_core_t`, `watchdog_should_feed(core)`, `watchdog_core_request_hang(core)`
- [ ] `components/watchdog/watchdog_core.c`: прапорець зависання; після запиту `watchdog_should_feed` = false
- [ ] `test/host/test_storage.c`, `test/host/test_watchdog.c`: усі комбінації результатів; запит зависання змінює рішення; повторний запит ідемпотентний
- [ ] `run_tests.sh` і `check_core_headers.sh` чисті

## Edge cases

| Case | Behaviour |
|---|---|
| Помилка сховища, яка не «немає сторінок» і не «нова версія» | не очищати; результат FAIL, стан UNAVAILABLE |
| Ініціалізація знову невдала після очищення | FAIL, стан UNAVAILABLE |
| Очищення успішне, повторна ініціалізація ok | стан WIPED, старт триває |
| Запит зависання двічі | ідемпотентно, `should_feed` лишається false |

## Definition of Done

- [ ] Хостові тести політик проходять (усі гілки рішення сховища)
- [ ] Непошкоджене сховище ніколи не отримує рішення очищення
- [ ] `check_core_headers.sh` чистий; `idf.py build` без нових попереджень
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
