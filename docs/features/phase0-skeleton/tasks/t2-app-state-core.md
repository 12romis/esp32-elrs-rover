---
id: T2
title: "Створити компонент app_state: таблиця переходів, запит переходу, історія (core)"
layer: "domain"
deps: ["T1"]
blocks: ["T3", "T5"]
acs: ["AC-06", "AC-01"]
files_hint: ["components/app_state/", "test/host/test_app_state.c"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "todo"
step: ""
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T2 — Створити компонент app_state: таблиця переходів, запит переходу, історія (core)

## Place in the sequence

- **Blocked by:** T1 — Створити хостовий раннер і скрипти перевірок test/host · **Blocks:** T3 — Створити console_core: збирання рядка, розбір команд, права за станом, T5 — Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS · **Wave:** 2.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник  
> **I want** після подачі живлення бачити в консолі, що автомат пройшов таблицю переходів до безпечного стану  
> **So that** я за секунди знаю, що прошивка запустилася і ровер у DISARMED
>
> — `spec.md §4, US-01, verbatim` · full text: [spec.md](../spec.md)

Дає чисту, тестовану на хості логіку автомата: 10 дозволених пар із 49, відмова з іменами станів, кільце історії на 16 записів.

## Inlined context

> Автомат станів: BOOT, SELFTEST, DISARMED, ARMED, OBSTACLE_HOLD, FAILSAFE, FAULT. Дозволені переходи (10 із 49): BOOT→SELFTEST; SELFTEST→DISARMED; SELFTEST→FAULT; DISARMED→ARMED; DISARMED→FAILSAFE; ARMED→DISARMED; ARMED→OBSTACLE_HOLD; ARMED→FAILSAFE; OBSTACLE_HOLD→ARMED; OBSTACLE_HOLD→FAILSAFE. Решта, включно з переходом у той самий стан, відхиляється; OBSTACLE_HOLD→DISARMED не дозволений; з FAILSAFE і FAULT виходів немає.
>
> — `spec.md §5, AC-06 (таблиця) + sad.md §4, журнал припущень, abridged` · full text: [spec.md](../spec.md)

> `app_state_core.c`: enum, імена, таблиця 7×7, запит переходу, кільце історії (16 записів). Core без блокувань, мʼютекс `m_state` лише в `app_state_hal`. Ідентифікатори `app_state_t` / `APP_STATE_*` не перейменовуються; `main/app_states.[ch]` переїжджає в `components/app_state/` (видалення старих файлів — у T8).
>
> — `sad.md §5, «Internal decomposition» + §4, журнал припущень, abridged` · full text: [sad.md](../sad.md)

> Рішення: розбити код фази 0 на компоненти `app_state`, `console`, `watchdog`, `storage`, кожен із `_core.c` + `_hal.c`.
>
> — `adr/0002, Decision outcome, abridged` · full text: [adr/0002](../adr/)

> Відмова логується через колбек, який підставляється в чисту логіку (на хості — перехоплення тексту), без додаткових команд консолі.
>
> — `spec.md §5, AC-06, примітка до перевірки, abridged` · full text: [spec.md](../spec.md)

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

### AC-06 (US-01) — domain invariant

> **Given** автомат у довільному стані  
> **When** запитано перехід, якого немає в таблиці дозволених (наприклад, з BOOT одразу в ARMED)  
> **Then** перехід відхилено, у консолі записано відмову з назвами обох станів, і автомат лишається у своєму стані; стан ARMED досяжний лише зі станів DISARMED і OBSTACLE_HOLD, як у таблиці переходів
>
> — `spec.md §5, AC-06, verbatim` · full text: [spec.md](../spec.md)

### AC-01 (US-01) — happy path

> **Given** плата з прошивкою фази 0 підключена до комп'ютера через USB, а моторні шлейфи не підключені  
> **When** Розробник подає живлення або скидає плату  
> **Then** у консолі видно стан BOOT, далі записи про кожен перехід (з якого стану в який) через SELFTEST у DISARMED, і плата лишається в DISARMED, приймаючи команди; ця ж історія переходів старту лишається доступною пізніше в команді показу стану, тож приймання не залежить від того, чи був хост підключений до перших рядків журналу
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `components/app_state/CMakeLists.txt`: `idf_component_register(SRCS "app_state_core.c" INCLUDE_DIRS "include")`
- [ ] `components/app_state/include/app_state.h`: `app_state_t`, `app_state_name()`, `app_sm_t`, `app_sm_result_t {APP_SM_OK, APP_SM_REFUSED}`, `app_log_fn`, функції `app_sm_init/app_sm_request/app_sm_state/app_sm_history_count/app_sm_history_get`
- [ ] `components/app_state/app_state_core.c`: статична таблиця `[7][7]` з 10 дозволеними парами; `app_sm_request(sm, to, t_ms)` приймає або відхиляє; відмова викликає `log` з текстом, що містить імена обох станів
- [ ] Кільце історії на 16 записів `{from,to,t_ms}`: при переповненні відкидає найстаріший
- [ ] `test/host/test_app_state.c`: перебір усіх 49 пар (рівно 10 ok, 39 refused), перевірка тексту відмови (обидва імена), self-transition відхилено, ARMED досяжний лише з DISARMED/OBSTACLE_HOLD, переповнення історії
- [ ] `test/host/run_tests.sh` і три перевірки з T1 проходять

## Edge cases

| Case | Behaviour |
|---|---|
| BOOT→ARMED | відхилено, у журналі «BOOT» і «ARMED», стан лишається BOOT |
| DISARMED→DISARMED | відхилено як «немає в таблиці», як будь-яке інше |
| OBSTACLE_HOLD→DISARMED | відхилено |
| Будь-який перехід зі стану FAILSAFE або FAULT | відхилено (виходів немає) |
| 17-й перехід підряд (історія) | найстаріший запис відкинуто, лишається 16 останніх |

## Definition of Done

- [ ] Хостовий тест підтверджує 49 з 49 перевірених пар (10 дозволених, 39 відхилених)
- [ ] Текст відмови містить імена обох станів; стан після відмови не змінюється
- [ ] `check_core_headers.sh` чистий; `idf.py build` без нових попереджень
- [ ] Hard rules вище виконано
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
