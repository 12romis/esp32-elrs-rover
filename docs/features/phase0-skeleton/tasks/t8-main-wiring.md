---
id: T8
title: "Скласти main.c: послідовність старту, REQUIRES, видалення main/app_states"
layer: "wiring"
deps: ["T5", "T6", "T7"]
blocks: ["T9"]
acs: ["AC-01", "AC-07", "AC-08"]
files_hint: ["main/", "components/board/include/"]
owner: "Агент"
estimate: "S"
context_budget: "M"
status: "done"
step: "COMMIT"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T8 — Скласти main.c: послідовність старту, REQUIRES, видалення main/app_states

## Place in the sequence

- **Blocked by:** T5 — Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS, T6 — Створити watchdog_hal: програмний panic для TWDT, задача heartbeat, причина скидання, T7 — Створити console_hal: ввід-вивід USB, задача console, виконання команд · **Blocks:** T9 — Підготувати чек-лист приймання фази 0 на залізі для журналу bring-up · **Wave:** 5.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник  
> **I want** щоб прошивка фази 0 не подавала ШІМ, не піднімала STBY і не зверталася до периферійної шини  
> **So that** плата з прошивкою фази 0 не може привести ровер у рух, поки моторні шлейфи не підключені й не прозвонені
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Зводить усе докупи: старт у BOOT, сторожовий таймер, консоль, сховище, переходи до DISARMED; жодних ліній моторів і шини периферії.

## Inlined context

> Старт: створити автомат у BOOT → увімкнути panic TWDT і heartbeat → стартувати консоль з колбеками → ініціалізувати сховище → якщо ok або очищене: запит BOOT→SELFTEST, SELFTEST→DISARMED; інакше стан лишається BOOT.
>
> — `sad.md §6, «Critical flow 1», abridged` · full text: [sad.md](../sad.md)

> `main.c` — лише ініціалізація й старт задач, без логіки. `main/app_states.[ch]` видаляються (переїхали в `components/app_state/`).
>
> — `sad.md §5, Internal decomposition + adr/0002, abridged` · full text: [sad.md](../sad.md)

> Фаза 0 не налаштовує жодної лінії ШІМ/напрямку/STBY і не звертається до шини розширювача; STBY низький лише завдяки апаратній притяжці R16.
>
> — `adr/0003, Decision outcome, abridged` · full text: [adr/0003](../adr/)

> 0 ліній ШІМ/напрямку/STBY налаштовано як виходи; STBY низький 100 % часу.
>
> — `spec.md §6, NFR «Безпека виходів», verbatim` · full text: [spec.md](../spec.md)

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

Internal — no API surface.

## Acceptance criteria

### AC-01 (US-01) — happy path

> **Given** плата з прошивкою фази 0 підключена до комп'ютера через USB, а моторні шлейфи не підключені  
> **When** Розробник подає живлення або скидає плату  
> **Then** у консолі видно стан BOOT, далі записи про кожен перехід (з якого стану в який) через SELFTEST у DISARMED, і плата лишається в DISARMED, приймаючи команди; ця ж історія переходів старту лишається доступною пізніше в команді показу стану, тож приймання не залежить від того, чи був хост підключений до перших рядків журналу
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

### AC-07 (US-04) — domain invariant

> **Given** моторні шлейфи не підключені, а розпіновку шлейфів ще не прозвонено  
> **When** прошивка фази 0 працює від старту і під час виконання будь-якої команди консолі, включно з перезавантаженням і навмисним зависанням  
> **Then** лінія STBY лишається низькою, ШІМ на мотори не подається, а обміну з розширювачем портів та іншою периферією по спільній шині не виникає
>
> — `spec.md §5, AC-07, verbatim` · full text: [spec.md](../spec.md)

### AC-08 (US-05) — happy path

> **Given** Агент завершив зміну в межах фази 0  
> **When** Агент запускає збірку проєкту і прогін хостових тестів  
> **Then** збірка завершується без помилок і без нових попереджень, а прогін повідомляє кількість тестів і що всі вони пройдені
>
> — `spec.md §5, AC-08, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `main/main.c`: `app_main` за послідовністю з sad §6; колбеки для консолі (`storage_status`, `request_hang`, `reset_reason`, `reboot`); при помилці `watchdog_start` чи сховища — журнал причини, автомат лишається в BOOT (за аналогією ADR-0004)
- [ ] `main/CMakeLists.txt`: `SRCS "main.c"`, `REQUIRES app_state rover_console watchdog storage` (без `board` пінів моторів)
- [ ] Видалити `main/app_states.c` і `main/app_states.h`
- [ ] `idf.py build`: 0 помилок, 0 нових попереджень; зафіксувати кількість попереджень до й після (spec §7)
- [ ] Огляд коду: `grep` по `main/` і `components/{app_state,console,watchdog,storage}` — немає `gpio_config`, `ledc`, `i2c`, `PIN_STBY`
- [ ] `test/host/run_tests.sh`, `check_core_headers.sh`, `check_pins.sh`, `check_protected.sh` — усі чисті

## Edge cases

| Case | Behaviour |
|---|---|
| `watchdog_start` або сховище повернули помилку | автомат лишається в BOOT, консоль працює, `status` показує причину |
| Старт без підключеного USB | старт доходить до DISARMED, журнал просто нікому читати |
| Поява нового попередження збірки | задача не вважається виконаною, Агент називає файл і причину |

## Definition of Done

- [ ] `idf.py build` без помилок і без нових попереджень; хостовий прогін і три перевірки чисті
- [ ] Огляд коду: жодних викликів налаштування ліній моторів і шини периферії (AC-07)
- [ ] `main/app_states.*` видалено; захищені файли не змінено
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
