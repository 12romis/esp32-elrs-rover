---
id: T5
title: "Створити session_core: межі сесії за станом, причина завершення, напруга, утримання за датчиками, час із вимкненим контролем, номер і тривалість"
layer: "domain"
deps: ["T0", "T2"]
blocks: ["T6"]
acs: ["AC-25"]
files_hint: ["components/session/session_core.c", "components/session/include/session_core.h", "test/host/test_session.c"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "todo"
step: ""
---

# T5 — Створити session_core: межі сесії за станом, причина завершення, напруга, утримання за датчиками, час із вимкненим контролем, номер і тривалість

## Place in the sequence

- **Blocked by:** T0 — Створити каркаси компонентів session і report: CMake, заголовки й порожні заглушки core/hal, що збираються без попереджень, T2 — Додати в tof_core маску датчиків нижче порога й розширити рядок утримання переліком усіх таких датчиків · **Blocks:** T6 — Додати в session_core пробіг, час руху, середню й пікову швидкість бортів і дисбаланс із явним «немає даних» · **Wave:** 2.
- **Lane:** shares `components/session/session_core.c` with T0, T6 — serialized.

## Why (user story)

> **As a** Розробник
> **I want** щоб після роззброєння ровер сам надсилав на сервер підсумок сесії
> **So that** я бачу історію виїздів і стан акумулятора без підключення USB
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: створити session_core: межі сесії за станом, причина завершення, напруга, утримання за датчиками, час із вимкненим контролем, номер і тривалість.

## Inlined context

> A5. **Сесія.** Одна сесія — від переходу в ARMED з DISARMED до наступного виходу з ARMED/OBSTACLE_HOLD у DISARMED або FAILSAFE (у FAULT з ARMED автомат не веде: він досяжний лише з SELFTEST). Перехід ARMED ↔ OBSTACLE_HOLD сесії не перериває. Сесії, у яких ровер озброївся, але жодного разу не отримав ненульової потужності, теж звітуються (час руху 0). Кожна сесія має порядковий номер, що не повторюється навіть після перезапуску плати, і тривалість: без цього два збережені звіти неможливо розрізнити, а повторну передачу — виявити.
>
> — `spec.md §1, A5, verbatim` · full text: [spec.md](../spec.md)

> A6. **Причина завершення сесії** — одне з: роззброєння (тумблером, консоллю чи зі стану, що вимагає роззброєння), FAILSAFE через втрату зв'язку, FAILSAFE через напругу. Це розширення запиту «LINK_LOST чи VOLTAGE чи ін»: «ін» зводиться до роззброєння, бо інших причин завершення сесії автомат не має. Чи відбувся вихід із FAILSAFE, звіт не фіксує (звіт записується один раз, у момент завершення).
>
> — `spec.md §1, A6, verbatim` · full text: [spec.md](../spec.md)

> A7. **Напруга** — зі знімка компонента батареї, у мілівольтах: на моменті входу в ARMED і на моменті виходу з ARMED/OBSTACLE_HOLD (для FAILSAFE через напругу — останнє значення перед відсічкою); додатково мінімум за сесію, бо він показує просадку під навантаженням (це понад запит, коштує один лічильник).
>
> — `spec.md §1, A7, verbatim` · full text: [spec.md](../spec.md)

> A8. **Утримання перед перешкодою** у звіті: кількість входів, сумарний час утримання (мс), мінімальна відстань, зафіксована при вході (`hold_mm`), датчик, що її дав (`hold_nearest`), і для кожного з чотирьох датчиків — у скількох утриманнях він був нижче порога **на вході в утримання** (так само, як у рядку консолі). Час режиму, коли контроль був вимкнений, теж у звіті (мс), щоб нуль утримань можна було відрізнити від вимкненого захисту.
>
> — `spec.md §1, A8, verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. Статистика рахується там, де дані вже є, без нових блокувань; приймач у черзі (задача `report`) отримує готову копію. Якщо черга повна (переповнення за ≤ 2 сесії), нова копія затирає найстарішу в черзі, як і правило з двох звітів.
>
> — `adr/0001-accumulate-session-statistics-in-the-control-task-and-hand-off-a-summary-through-a-bounded-queue.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> Flow 1/9: ARMED → початок сесії з напругою старту; кожен крок 20 мс: стан, швидкості, потужність, події утримання; вихід у DISARMED/FAILSAFE → кінець сесії з напругою виходу; підсумок через чергу.
>
> — `sad.md §6, «Flow 1», «Flow 9», abridged` · full text: [sad.md](../sad.md)

> Конституція, п. 6: кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`; логіка живе в core і покривається тестами на хості. Пп. 8–9: збірка без помилок і без нових попереджень; коментарі й коміти — українською, ідентифікатори — англійською.
>
> — `docs/constitution.md, пп. 6, 8, 9, abridged` · full text: [constitution.md](../../../constitution.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Внутрішнє: структура `session_summary_t` — джерело полів документа (events.md, «Payload»).

## Acceptance criteria

### AC-25 — happy path

> **Given** сесія зі збереженим номером, мінімальною напругою, утриманнями за двома датчиками, часом із вимкненим контролем і тривалістю
> **When** Розробник дивиться на звіт цієї сесії
> **Then** звіт містить порядковий номер сесії, тривалість, мінімальну напругу, лічильник утримань для кожного датчика і час, коли контроль був вимкнений
>
> — `spec.md §5, AC-25, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `session_core.h/.c`: `session_t` + `session_step(session_t*, const session_in_t*)` (чиста функція від входів кроку: `now_ms`, `state`, `failsafe_cause`, `battery_mv`/`battery_valid`, `hold_enter` (+`hold_mm`, `hold_nearest`, маска датчиків), `tof_control_on`); `session_summary_t` з полями: `seq`, `duration_ms`, `end_reason` (`disarm|failsafe_link_lost|failsafe_voltage`), `voltage_start_mv`, `voltage_end_mv`, `voltage_min_mv` (з прапорцями валідності), `holds` (кількість, сумарний час, мінімум і датчик мінімуму, лічильники по 4 датчиках), `tof_control_off_ms`.
- [ ] Межі: початок — перехід DISARMED → ARMED (або DISARMED → OBSTACLE_HOLD); кінець — вихід з ARMED/OBSTACLE_HOLD у DISARMED чи FAILSAFE; перехід ARMED ↔ OBSTACLE_HOLD сесії не перериває; сесія без ненульової потужності теж звітується.
- [ ] Номер сесії задає зовнішній лічильник (T11); `session_core` приймає `next_seq` при початку.
- [ ] Хостові тести `test/host/test_session.c`: життєвий цикл, всі три причини, мінімальна напруга, застарілий знімок → прапорець невалідності, кілька утримань (лічильники й мінімум), час із вимкненим контролем.

## Edge cases

| Case | Behaviour |
|---|---|
| FAILSAFE через напругу | `voltage_end_mv` — остання напруга перед відсічкою |
| ARMED ↔ OBSTACLE_HOLD | сесія триває, утримання додається до лічильників |
| знімок напруги застарів | відповідне поле позначене невалідним (у JSON — `null`) |
| озброєно без руху | сесія звітується з `moving_ms` = 0 (T6) |

## Definition of Done

- [ ] хостові тести session проходять; `session_core.c` без заголовків ESP-IDF
- [ ] `idf.py build` без нових попереджень
- [ ] кожне Hard Rule вище лишається дійсним
