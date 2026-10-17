---
id: T3
title: "Створити console_core: збирання рядка, розбір команд, права за станом"
layer: "app"
deps: ["T1", "T2"]
blocks: ["T7"]
acs: ["AC-02", "AC-03", "AC-05"]
files_hint: ["components/console/", "test/host/test_console.c"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "todo"
step: ""
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T3 — Створити console_core: збирання рядка, розбір команд, права за станом

## Place in the sequence

- **Blocked by:** T1 — Створити хостовий раннер і скрипти перевірок test/host, T2 — Створити компонент app_state: таблиця переходів, запит переходу, історія (core) · **Blocks:** T7 — Створити console_hal: ввід-вивід USB, задача console, виконання команд · **Wave:** 3.
- **Lane:** own lane (каталог console спільний із T7 — послідовно).

## Why (user story)

> **As a** Розробник  
> **I want** виводити стан, довідку та перезавантажувати плату командами в консолі  
> **So that** я можу діагностувати плату з комп'ютера, не відкриваючи відлагоджувач
>
> — `spec.md §4, US-02, verbatim` · full text: [spec.md](../spec.md)

Дає чисту логіку консолі: рядок до 64 символів, розбір `help/status/reboot/wdt-test`, відмови для невідомих, зайвих аргументів і заборонених станів.

## Inlined context

> Ліміт рядка 64 символи; довший відкидається повністю з поясненням і нічого не виконує. Назви команд розрізняють регістр; зайвий аргумент відхиляється; рядок лише з пробілів = порожній.
>
> — `spec.md §5, AC-03 + sad.md §8, «Console input», abridged` · full text: [spec.md](../spec.md)

> Команди: `help`, `status`, `reboot`, `wdt-test`. `help` і `status` дозволені в кожному стані; `reboot` — у DISARMED і BOOT; `wdt-test` — лише в DISARMED.
>
> — `spec.md §5, AC-02 і AC-05, abridged` · full text: [spec.md](../spec.md)

> Консоль залежить від `app_state`, але не від `watchdog` і `storage`; потрібні дії передає `main` структурою колбеків.
>
> — `adr/0002, Decision outcome, abridged` · full text: [adr/0002](../adr/)

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

Командний інтерфейс консолі (єдина зовнішня поверхня, `cli`):

| Команда | Дозволена в | Результат |
|---|---|---|
| `help` | усіх станах | перелік: help, status, reboot, wdt-test |
| `status` | усіх станах | стан, час роботи, вільна внутрішня памʼять, причина скидання, стан сховища, історія переходів |
| `reboot` | DISARMED і BOOT | попередження і перезавантаження |
| `wdt-test` | лише DISARMED | попередження і навмисне зависання службової задачі |

— `spec.md §5, AC-02 / AC-05 / AC-13, abridged` · full text: [spec.md](../spec.md)

## Acceptance criteria

### AC-02 (US-02) — happy path

> **Given** автомат у стані DISARMED і консоль активна  
> **When** Розробник вводить команду показу стану, а потім команду довідки  
> **Then** консоль виводить поточний стан, час роботи, обсяг вільної внутрішньої пам'яті (зіставний із baseline P0; повний обсяг разом із зовнішньою пам'яттю може додаватися лише з окремою позначкою), причину останнього скидання та історію переходів автомата від старту, а довідка перелічує всі доступні команди: help, status, reboot, wdt-test
>
> — `spec.md §5, AC-02, verbatim` · full text: [spec.md](../spec.md)

### AC-03 (US-02) — error

> **Given** консоль активна  
> **When** Розробник вводить невідому команду, порожній рядок, рядок, довший за допустиму довжину, або відому команду з зайвим аргументом (наприклад, `status foo`)  
> **Then** на невідому команду консоль відповідає, що команди немає, і підказує довідку; порожній рядок (зокрема рядок лише з пробілів) ігнорується; надто довгий рядок відкидається повністю з поясненням, і жодна його частина не виконується; відома команда з зайвим аргументом відхиляється з поясненням; назви команд розрізняють регістр (`HELP` — невідома команда); в усіх випадках консоль лишається працездатною, а стан автомата не змінюється
>
> — `spec.md §5, AC-03, verbatim` · full text: [spec.md](../spec.md)

### AC-05 (US-03) — cross-context

> **Given** автомат у стані, відмінному від DISARMED  
> **When** Розробник вводить команду навмисного зависання, або (у стані, відмінному і від BOOT) команду перезавантаження  
> **Then** команду відхилено з поясненням: навмисне зависання дозволене лише в стані DISARMED, а перезавантаження — лише в станах DISARMED і BOOT; служба захисту від зависання та стан автомата лишаються без змін; команди help і status дозволені в кожному стані
>
> — `spec.md §5, AC-05, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `components/console/CMakeLists.txt`: `idf_component_register(SRCS "console_core.c" INCLUDE_DIRS "include" REQUIRES app_state)`
- [ ] `components/console/include/console_core.h`: `CONSOLE_LINE_MAX 64`, `console_t`, події `{NONE, LINE, OVERFLOW}`, `console_cmd_t {EMPTY, UNKNOWN, EXTRA_ARGS, DENIED_STATE, HELP, STATUS, REBOOT, WDT_TEST}`
- [ ] `console_core.c`: `console_feed(c, byte)` збирає рядок (CR/LF = кінець, backspace стирає), при перевищенні 64 відкидає рядок до кінця і повертає OVERFLOW
- [ ] `console_core.c`: `console_parse(line, state)` — токенізація по пробілах, регістр розрізняється, зайві аргументи → EXTRA_ARGS, права за станом → DENIED_STATE
- [ ] `console_core.c`: тексти відповідей (невідома команда + підказка про `help`, довідка зі списком команд, відмова «лише в DISARMED», переповнення) — українською
- [ ] `test/host/test_console.c`: порожній і «лише пробіли», 64 і 65 символів, `HELP`, `status foo`, `wdt-test` у кожному зі станів, `reboot` у кожному зі станів (дозволено лише DISARMED і BOOT), `help`/`status` у кожному зі станів
- [ ] `run_tests.sh` і `check_core_headers.sh` чисті

## Edge cases

| Case | Behaviour |
|---|---|
| Порожній рядок або лише пробіли | ігнорується, відповіді немає |
| Рядок 65 символів | відкинуто повністю з поясненням, жодна частина не виконана, консоль працює |
| `HELP` | невідома команда + підказка про `help` |
| `status foo` | відхилено з поясненням (зайвий аргумент) |
| `wdt-test` не в DISARMED; `reboot` не в DISARMED і не в BOOT | відхилено з поясненням, де команда дозволена; стан і сторожовий таймер без змін |
| `reboot` у BOOT (сховище не ініціалізувалося) | виконується: попередження і перезавантаження |

## Definition of Done

- [ ] Хостові тести AC-02, AC-03, AC-05 проходять
- [ ] `console_parse` повертає DENIED_STATE для `wdt-test` у кожному стані, окрім DISARMED, і для `reboot` у кожному стані, окрім DISARMED і BOOT
- [ ] `check_core_headers.sh` чистий; `idf.py build` без нових попереджень
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
