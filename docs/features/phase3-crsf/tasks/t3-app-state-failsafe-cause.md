---
id: T3
title: "Додати в app_state причину FAILSAFE, підвищення до «напруга» і охорону переходу FAILSAFE → DISARMED"
layer: "domain"
deps: []
blocks: ["T4", "T6"]
acs: ["AC-08", "AC-09", "AC-17"]
files_hint: ["components/app_state/app_state_core.c", "components/app_state/include/app_state.h", "components/app_state/app_state_hal.c", "components/app_state/include/app_state_hal.h", "test/host/test_app_state.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "M"
status: "done"
step: "COMMIT"
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T3 — Додати в app_state причину FAILSAFE, підвищення до «напруга» і охорону переходу FAILSAFE → DISARMED

## Place in the sequence

- **Blocked by:** — · **Blocks:** T4 — Розширити drive_ctl_step: стендовий режим, таймер зв'язку, втрата зв'язку в ARMED і DISARMED, відмова консолі «керує пульт», T6 — Розширити drive_ctl_step: вихід з FAILSAFE після ≥ 1 с стабільного зв'язку з тумблером у DISARM і відсічка у FAILSAFE · **Wave:** 1.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** щоб після відновлення зв'язку ровер виходив з FAILSAFE у DISARMED, коли тумблер у DISARM
> **So that** мені не треба йти до ровера й скидати плату після кожного короткого зникнення зв'язку
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Автомат сам не дає вийти з FAILSAFE після відсічки по напрузі, а вихід після втрати зв'язку стає можливим.

## Inlined context

> Таблиця переходів автомата: 11 дозволених пар із 49 — до 10 пар фази 0 додано FAILSAFE → DISARMED, який автомат виконує лише за умов AC-08 (причина «втрата зв'язку», не замінена на «напруга»; ≥ 1 с стабільного зв'язку; тумблер у DISARM). Запит цього переходу за інших умов відхиляється.
>
> — `spec.md §5, примітка до таблиці переходів, abridged` · full text: [spec.md](../spec.md)

> `app_state` приймає вхід у FAILSAFE разом із причиною («втрата зв'язку» / «напруга»), підвищує її до «напруга» при повторному повідомленні і сам відхиляє FAILSAFE → DISARMED для будь-якої причини, крім «втрата зв'язку». Умови з часом (≥ 1 с стабільного зв'язку, тумблер у DISARM) рахує `control`.
>
> — `sad.md §4, пункт 2, verbatim` · full text: [sad.md](../sad.md)

> **Chosen:** варіант 1. Правило безпеки «після відсічки — лише скидання» тоді захищене в одному місці, яке бачать усі викликачі переходів, і покривається тестами таблиці автомата.
>
> — `adr/0002, Decision outcome, abridged` · full text: [adr/0002-keep-failsafe-cause-in-state-machine-and-guard-recovery-there.md](../adr/0002-keep-failsafe-cause-in-state-machine-and-guard-recovery-there.md)

> Змінюється інтерфейс `app_state` фази 0: новий вхід у FAILSAFE із причиною; наявні виклики FAILSAFE у `control` (відсічка) переходять на нього з причиною «напруга».
>
> — `adr/0002, Consequences, verbatim` · full text: [adr/0002-keep-failsafe-cause-in-state-machine-and-guard-recovery-there.md](../adr/0002-keep-failsafe-cause-in-state-machine-and-guard-recovery-there.md)

> **Hard rule:** Кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`. Логіка живе в core і покривається тестами на хості.
>
> — `constitution.md, п. 6, abridged` · full text: [../../constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-08 — happy path

> **Given** ровер у FAILSAFE з причиною «втрата зв'язку», і за час перебування в FAILSAFE відсічка по напрузі не спрацювала (причина не змінилася на «напруга», AC-09)
> **When** зв'язок відновлюється й лишається безперервно живим щонайменше 1 с, а тумблер у DISARM (або Розробник переводить його в DISARM); відлік 1 с починається з першого валідного кадру після втрати, скидається лише новою втратою зв'язку (окремі пропущені кадри його не скидають) і стосується тільки зв'язку: якщо зв'язок живий уже ≥ 1 с, а тумблер стояв у ARM, вихід відбувається одразу, щойно тумблер опинився в DISARM
> **Then** ровер переходить у DISARMED із сигналом зумера фази 2, блимання й червоний індикатор гаснуть, консоль повідомляє про вихід з аварійної зупинки й причину; ровер не їде й не озброюється сам, наступне озброєння — лише за AC-03
>
> — `spec.md §5, AC-08, verbatim` · full text: [spec.md](../spec.md)

### AC-09 — domain invariant

> **Given** ровер у FAILSAFE
> **When** зв'язок відновився, але тумблер лишився в ARM; або зв'язок знову зник раніше ніж за 1 с безперервної роботи; або причиною FAILSAFE є напруга нижче порога (зокрема якщо відсічка фази 1 — з тим самим усередненням, що й у русі — спрацювала, поки ровер уже був у FAILSAFE через зв'язок: тоді причина назавжди замінюється на «напруга», і подальше відновлення напруги вже нічого не змінює)
> **Then** ровер лишається у FAILSAFE, STBY внизу, мотори не крутяться; консоль називає умову, якої бракує (тумблер у DISARM, стабільний зв'язок) або пояснює, що після відсічки по напрузі вихід можливий лише апаратним скиданням плати чи перемиканням живлення; правило «вихід лише скиданням» для напруги не змінюється відносно фази 1
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

### AC-17 — cross-context

> **Given** ровер озброєний з пульта й їде
> **When** напруга акумулятора падає нижче порога відсічки
> **Then** ровер зупиняється й переходить у FAILSAFE з причиною «напруга», як у фазі 1, незалежно від положення стіка й тумблера; вихід лише апаратним скиданням (AC-09)
>
> — `spec.md §5, AC-17, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `app_state.h`: перелік причин {немає, втрата зв'язку, напруга}, поле причини в `app_sm_t`, `app_sm_enter_failsafe(sm, cause, t_ms)` і `app_sm_failsafe_cause(sm)`. Старий `app_sm_request` лишається: його запит FAILSAFE трактується як причина «напруга» (безпечний бік), тож `drive` фази 1 компілюється без змін до T4.
- [ ] `app_state_core.c`: таблиця 11 пар (FAILSAFE → DISARMED) з коментарем про охорону; `app_sm_request(DISARMED)` з FAILSAFE відхиляється, якщо причина ≠ втрата зв'язку; повторний вхід у FAILSAFE з причиною «напруга» в FAILSAFE підвищує причину без запису в історію; «втрата зв'язку» не знижує «напруга».
- [ ] `app_state_hal`: обгортки під мʼютексом `m_state` і журнал (підвищення причини — один рядок).
- [ ] Тести `test/host/test_app_state.c`: 49 пар (11 дозволених), охорона за причиною, підвищення причини, причина після виходу скидається.

## Edge cases

| Case | Behaviour |
|---|---|
| FAILSAFE з причиною «напруга», запит DISARMED | відхилено, стан незмінний |
| FAILSAFE через зв'язок, потім повідомлення «напруга» | причина назавжди «напруга» |
| FAILSAFE «напруга», потім повідомлення «втрата зв'язку» | причина лишається «напруга» |
| Вихід FAILSAFE → DISARMED | причина скидається до «немає» |

## Definition of Done

- [ ] Тести автомата зелені: 49 з 49 пар, охорона і підвищення причини
- [ ] Наявний код фаз 0–2 збирається без змін
- [ ] `idf.py build` без нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
