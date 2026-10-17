---
id: T7
title: "Створити console_hal: ввід-вивід USB, задача console, виконання команд"
layer: "ports"
deps: ["T3", "T5"]
blocks: ["T8"]
acs: ["AC-01", "AC-02", "AC-13"]
files_hint: ["components/rover_console/"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "done"
step: "COMMIT"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T7 — Створити console_hal: ввід-вивід USB, задача console, виконання команд

## Place in the sequence

- **Blocked by:** T3 — Створити console_core: збирання рядка, розбір команд, права за станом, T5 — Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS · **Blocks:** T8 — Скласти main.c: послідовність старту, REQUIRES, видалення main/app_states · **Wave:** 4.
- **Lane:** own lane (components/rover_console/ після T3).

## Why (user story)

> **As a** Розробник  
> **I want** виводити стан, довідку та перезавантажувати плату командами в консолі  
> **So that** я можу діагностувати плату з комп'ютера, не відкриваючи відлагоджувач
>
> — `spec.md §4, US-02, verbatim` · full text: [spec.md](../spec.md)

Робить консоль живою: читає USB, збирає рядок через core, виконує `help/status/reboot/wdt-test` через колбеки від `main`.

## Inlined context

> `console_hal.c`: задача `console` (core 0, пріоритет 1, блокується на вводі), читання/запис USB-Serial-JTAG, виконання команд. Дії від інших модулів — структура колбеків, яку передає `main`: стан сховища, запит зависання, перезавантаження, причина скидання.
>
> — `sad.md §5 і §4, abridged` · full text: [sad.md](../sad.md)

> `status`: стан, час роботи, вільна внутрішня памʼять (зіставна з baseline P0 — `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`), причина скидання, стан сховища, історія переходів від старту.
>
> — `spec.md §5, AC-02 + sad.md §6 flow 1, abridged` · full text: [spec.md](../spec.md)

> Ризик: читання вводу з USB-Serial-JTAG (драйвер, взаємодія з виводом) не перевірене на залізі; core не змінюється, якщо доведеться змінити спосіб читання.
>
> — `sad.md §11, перший ризик, abridged` · full text: [sad.md](../sad.md)

> `reboot`: попередження, потім перезавантаження; причина скидання після старту — програмне перезавантаження.
>
> — `spec.md §5, AC-13, abridged` · full text: [spec.md](../spec.md)

> **Hard rule:** номери GPIO існують лише в `components/board/include/board_pins.h`; літерал номера лінії деінде — дефект. Фаза 0 не налаштовує жодної лінії мотора і не посилається на `PIN_STBY`.
>
> — `constitution.md, п. 2 і ADR-0003, abridged` · full text: [constitution.md](../../../constitution.md)

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

Командний інтерфейс — див. таблицю команд у T3 (`help`, `status`, `reboot`, `wdt-test`); тут його виконує HAL.

— `spec.md §5, AC-02 / AC-13, abridged` · full text: [spec.md](../spec.md)

## Acceptance criteria

### AC-01 (US-01) — happy path

> **Given** плата з прошивкою фази 0 підключена до комп'ютера через USB, а моторні шлейфи не підключені  
> **When** Розробник подає живлення або скидає плату  
> **Then** у консолі видно стан BOOT, далі записи про кожен перехід (з якого стану в який) через SELFTEST у DISARMED, і плата лишається в DISARMED, приймаючи команди; ця ж історія переходів старту лишається доступною пізніше в команді показу стану, тож приймання не залежить від того, чи був хост підключений до перших рядків журналу
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

### AC-02 (US-02) — happy path

> **Given** автомат у стані DISARMED і консоль активна  
> **When** Розробник вводить команду показу стану, а потім команду довідки  
> **Then** консоль виводить поточний стан, час роботи, обсяг вільної внутрішньої пам'яті (зіставний із baseline P0; повний обсяг разом із зовнішньою пам'яттю може додаватися лише з окремою позначкою), причину останнього скидання та історію переходів автомата від старту, а довідка перелічує всі доступні команди: help, status, reboot, wdt-test
>
> — `spec.md §5, AC-02, verbatim` · full text: [spec.md](../spec.md)

### AC-13 (US-02) — happy path

> **Given** автомат у стані DISARMED або BOOT (наприклад, після невдалої ініціалізації сховища) і консоль активна  
> **When** Розробник вводить команду перезавантаження  
> **Then** консоль попереджає про перезавантаження, плата перезавантажується, проходить старт до DISARMED (AC-01), а команда показу стану називає причиною останнього скидання програмне перезавантаження
>
> — `spec.md §5, AC-13, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `components/rover_console/include/console.h`: `console_ops_t {storage_status, request_hang, reset_reason, reboot}` і `console_start(ops)`
- [ ] `components/rover_console/console_hal.c`: читання байтів USB-Serial-JTAG, ехо, подача в `console_feed`; на LINE — `console_parse` з поточним станом; на OVERFLOW — повідомлення
- [ ] Виконання: `help` (текст core), `status` (поля з AC-02), `reboot` (дозволений у DISARMED і BOOT: попередження, `fflush`, `esp_restart`), `wdt-test` (попередження, `ops->request_hang`)
- [ ] Оновити `components/rover_console/CMakeLists.txt`: `console_hal.c`, `REQUIRES app_state esp_driver_usb_serial_jtag esp_timer esp_system`
- [ ] `idf.py build`: 0 помилок, 0 нових попереджень; без викликів конфігурації GPIO моторів

## Edge cases

| Case | Behaviour |
|---|---|
| Ввід відсутній (USB не підключено) | задача блокується на читанні, нічого не друкує, не падає |
| Надто довгий рядок | пояснення, консоль працює далі, стан не змінено |
| `reboot` у DISARMED | попередження, перезавантаження, `status` після старту: причина «SW» |
| `status` у кожному стані | відповідає завжди; історія містить переходи старту |

## Definition of Done

- [ ] `idf.py build` без помилок і без нових попереджень
- [ ] `status` виводить стан, час роботи, внутрішню памʼять, причину скидання, стан сховища, історію
- [ ] Hard rules вище виконано; приймання «консоль відповідає» — запис у журнал bring-up за Розробником
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
