---
id: T5
title: "Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS"
layer: "infra"
deps: ["T2", "T4"]
blocks: ["T7", "T8"]
acs: ["AC-12", "AC-01"]
files_hint: ["components/app_state/", "components/storage/"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "todo"
step: ""
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T5 — Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS

## Place in the sequence

- **Blocked by:** T2 — Створити компонент app_state: таблиця переходів, запит переходу, історія (core), T4 — Створити storage_core і watchdog_core: політика сховища та політика годування · **Blocks:** T7 — Створити console_hal: ввід-вивід USB, задача console, виконання команд, T8 — Скласти main.c: послідовність старту, REQUIRES, видалення main/app_states · **Wave:** 3.
- **Lane:** own lane (components/app_state/ після T2, components/storage/ після T4).

## Why (user story)

> **As a** Розробник  
> **I want** щоб прошивка пережила пошкоджене сховище налаштувань на платі  
> **So that** старий вміст після невдалого прошивання не блокує старт
>
> — `spec.md §4, US-07, verbatim` · full text: [spec.md](../spec.md)

Приєднує логіку до заліза: мʼютекс `m_state`, друк кожного переходу в журнал, `nvs_flash_init/erase` за рішенням core, без `abort()`.

## Inlined context

> `app_state_hal.c`: мʼютекс `m_state`, час старту (`esp_timer`), журнал переходів. Усі збої повертаються як `esp_err_t`; `ESP_ERROR_CHECK` лише для справді фатальних збоїв, не для сховища.
>
> — `sad.md §5 і §8 «Error handling», abridged` · full text: [sad.md](../sad.md)

> Старт: створити автомат у BOOT; сховище в порядку або очищене → BOOT→SELFTEST→DISARMED з журналом кожного переходу; інакше журнал причини, стан лишається BOOT.
>
> — `sad.md §6, «Critical flow 1», abridged` · full text: [sad.md](../sad.md)

> Рішення: лишатися в BOOT з працюючою консоллю, якщо сховище не ініціалізується; без `abort()` і без циклу перезавантажень.
>
> — `adr/0004, Decision outcome, abridged` · full text: [adr/0004](../adr/)

> **Hard rule:** номери GPIO існують лише в `components/board/include/board_pins.h`; літерал номера лінії деінде — дефект. Фаза 0 не налаштовує жодної лінії мотора і не посилається на `PIN_STBY`.
>
> — `constitution.md, п. 2 і ADR-0003, abridged` · full text: [constitution.md](../../../constitution.md)

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

### AC-01 (US-01) — happy path

> **Given** плата з прошивкою фази 0 підключена до комп'ютера через USB, а моторні шлейфи не підключені  
> **When** Розробник подає живлення або скидає плату  
> **Then** у консолі видно стан BOOT, далі записи про кожен перехід (з якого стану в який) через SELFTEST у DISARMED, і плата лишається в DISARMED, приймаючи команди; ця ж історія переходів старту лишається доступною пізніше в команді показу стану, тож приймання не залежить від того, чи був хост підключений до перших рядків журналу
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `components/app_state/app_state_hal.c`: `app_state_hal_init()`, `app_state_request(to)` (захоплює `m_state`, бере `esp_timer_get_time()/1000`, викликає core, друкує `ESP_LOGI` для OK і `ESP_LOGW` для відмови з іменами обох станів), `app_state_get()`, копіювання історії
- [ ] Оновити `components/app_state/CMakeLists.txt`: додати `app_state_hal.c`, `REQUIRES esp_timer` (FreeRTOS мʼютекс)
- [ ] `components/storage/storage_hal.c`: `storage_init()` — `nvs_flash_init`, мапінг `esp_err_t`→`storage_result_t`, за рішенням core `nvs_flash_erase` + повтор, `ESP_LOGW` про очищення, `ESP_LOGE` з `esp_err_to_name` про збій; повертає `storage_status_t`; без `ESP_ERROR_CHECK`
- [ ] Оновити `components/storage/CMakeLists.txt`: `storage_hal.c`, `REQUIRES nvs_flash`
- [ ] `idf.py build`: 0 помилок, 0 нових попереджень

## Edge cases

| Case | Behaviour |
|---|---|
| NVS повертає «немає вільних сторінок» | очищення, повтор, стан WIPED, повідомлення в журналі |
| NVS повертає іншу помилку | сховище не очищається, `ESP_LOGE` з назвою помилки, стан UNAVAILABLE |
| Запит забороненого переходу | `ESP_LOGW` з іменами обох станів, стан без змін |
| Дві задачі одночасно просять перехід | serialize через `m_state`, таблиця перевіряється під мʼютексом |

## Definition of Done

- [ ] `idf.py build` без помилок і без нових попереджень
- [ ] HAL не містить літералів пінів і не викликає конфігурацію ліній моторів
- [ ] Приймання на залізі (AC-12 з пошкодженим сховищем) — запис у журнал bring-up лишається за Розробником (spec §8, OQ-6)
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
