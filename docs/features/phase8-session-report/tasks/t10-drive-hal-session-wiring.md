---
id: T10
title: "Підʼєднати session до drive_hal: виклик session_step наприкінці кроку керування, подачу підсумку в чергу й логування подій режиму"
layer: "wiring"
deps: ["T3", "T6"]
blocks: ["T13", "T14"]
acs: ["AC-12", "AC-19"]
files_hint: ["components/drive/drive_hal.c", "components/session/session_hal.c", "components/session/include/session_hal.h", "components/drive/CMakeLists.txt"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T10 — Підʼєднати session до drive_hal: виклик session_step наприкінці кроку керування, подачу підсумку в чергу й логування подій режиму

## Place in the sequence

- **Blocked by:** T3 — Додати в drive_core вхід «режим контролю» і подавити зайнятість секторів, коли режим вимкнено, з подіями зміни режиму й охорони, T6 — Додати в session_core пробіг, час руху, середню й пікову швидкість бортів і дисбаланс із явним «немає даних» · **Blocks:** T13 — Створити report_hal: задачу report на core 0 з спробою передачі при вході в DISARMED, скасуванням озброєнням і записом результатів, T14 — Додати в консоль команду report та поля status про режим контролю й звіти, з підключенням у main · **Wave:** 4.
- **Lane:** shares `components/session/session_hal.c` with T0 — serialized.

## Why (user story)

> **As a** Розробник
> **I want** щоб після роззброєння ровер сам надсилав на сервер підсумок сесії
> **So that** я бачу історію виїздів і стан акумулятора без підключення USB
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)


> **As a** Розробник
> **I want** щоб Wi-Fi працював лише в DISARMED, не заважав озброєнню і вимикався одразу після передачі
> **So that** радіо ніколи не впливає на безпеку й живлення під час руху
>
> — `spec.md §4, US-06, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: підʼєднати session до drive_hal: виклик session_step наприкінці кроку керування, подачу підсумку в чергу й логування подій режиму.

## Inlined context

> A14. **Озброєння має пріоритет над передачею.** Якщо під час спроби прийшов запит на озброєння, спробу негайно перериває: з'єднання закривається, Wi-Fi вимикається, звіт лишається невідправленим, озброєння відбувається у звичайний спосіб і темпі. Передача виконується в окремій низькопріоритетній задачі на core 0 разом із Wi-Fi-стеком (документ 02 §6.1), керування лишається на core 1; жоден такт керування, зумер, світло і консоль не чекають на мережу. Ця специфікація свідомо доповнює фазу 6 (ADR-0004 phase6-tof про «захист завжди увімкнений») і фазу 7 (індикатор): відповідні рішення оформить `design`.
>
> — `spec.md §1, A14, verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. Статистика рахується там, де дані вже є, без нових блокувань; приймач у черзі (задача `report`) отримує готову копію. Якщо черга повна (переповнення за ≤ 2 сесії), нова копія затирає найстарішу в черзі, як і правило з двох звітів.
>
> — `adr/0001-accumulate-session-statistics-in-the-control-task-and-hand-off-a-summary-through-a-bounded-queue.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> | Вплив на керування | 0 нових кроків керування довших за період 20 мс під час спроби; озброєння не затримується; виняток — моменти запису в памʼять плати (≤ 2 на сесію), коли крок і озброєння можуть затриматись на тривалість запису (як `pid save`) | `status` (час кроку) до і після спроби |
>
> — `spec.md §6, «Вплив на керування», verbatim` · full text: [spec.md](../spec.md)

> Конституція, п. 7: STBY піднімається тільки в ARMED; втрата кадрів CRSF довша за 500 мс → ШІМ у нуль, STBY вниз; перша операція по I²C після старту — запис 0x00 у PCF8574; VBAT < 6.0 В → мотори не запускаються. Інваріанти сильніші за будь-яку функціональність.
>
> — `docs/constitution.md, п. 7, abridged` · full text: [constitution.md](../../../constitution.md)

> Конституція, п. 14: усі періодичні задачі використовують vTaskDelayUntil, а не vTaskDelay; шина I²C працює на 100 кГц.
>
> — `docs/constitution.md, п. 14, abridged` · full text: [constitution.md](../../../constitution.md)

> Flow 3: втрата зв'язку понад 0.5 с → кінець сесії, причина «втрата зв'язку» → підсумок через чергу → звіт зберігається; FAILSAFE, Wi-Fi не вмикається; вихід у DISARMED (зв'язок відновлено) → звіт передається як у потоці 1.
>
> — `sad.md §6, «Flow 3», abridged` · full text: [sad.md](../sad.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Внутрішнє: черга підсумків між `control` (core 1) і `report` (core 0); консольних змін немає.

## Acceptance criteria

### AC-12 — cross-context

> **Given** сесію завершив FAILSAFE через втрату зв'язку, а потім Розробник повернув ровер у DISARMED
> **When** ровер входить у DISARMED
> **Then** звіт цієї сесії позначає причину «втрата зв'язку» і надсилається так само, як після звичайного роззброєння
>
> — `spec.md §5, AC-12, verbatim` · full text: [spec.md](../spec.md)

### AC-19 — cross-context

> **Given** ровер у будь-якому стані, крім DISARMED
> **When** Розробник перевіряє стан радіо
> **Then** Wi-Fi вимкнений у BOOT, SELFTEST, ARMED, OBSTACLE_HOLD, FAILSAFE і FAULT, а сама передача не затримує жодного такту керування і не змінює стан автомата
>
> — `spec.md §5, AC-19, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `session_hal.h/.c`: `session_hal_init` (чер­га довжини 2 `session_summary_t`, без блокувань; повна черга → найстаріший затирається), `session_hal_step(const session_in_t*)` — виклик `session_step` у задачі control і, коли сесія завершилась, відправка копії підсумку без очікування; `session_hal_set_next_seq` (джерело — T11).
- [ ] `drive_hal.c`: після `drive_ctl_step` і до `esp_task_wdt_reset` зібрати `session_in_t` (стан, причина FAILSAFE, знімок батареї, `tof_view_t`, `enc_state_t`, застосовані permille, `tof_control_on` з `crsf_tof_control_effective`) і викликати `session_hal_step`; передати `tof_control_on` у `drive_ctl_in_t`; логувати події T3 (`ESP_LOGW` на зміну режиму і охорони).
- [ ] Не додавати мʼютексів і блокуючих викликів у гарячий шлях; виміряти крок існуючою статистикою (`status`).
- [ ] `components/drive/CMakeLists.txt`: REQUIRES `session`.

## Edge cases

| Case | Behaviour |
|---|---|
| черга підсумків повна | найстаріший затирається, керування не чекає |
| FAILSAFE через втрату зв'язку | сесія завершується з причиною «втрата зв'язку», підсумок у черзі |
| FAILSAFE через напругу | те саме з причиною «напруга» |
| крок керування | без нових мʼютексів; 0 нових кроків довших за 20 мс |

## Definition of Done

- [ ] `idf.py build` без нових попереджень; `status` показує час кроку без нових перевищень на стенді
- [ ] хостові тести drive і session проходять
- [ ] кожне Hard Rule вище лишається дійсним
