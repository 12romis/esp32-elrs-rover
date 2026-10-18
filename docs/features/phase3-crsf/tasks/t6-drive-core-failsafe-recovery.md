---
id: T6
title: "Розширити drive_ctl_step: вихід з FAILSAFE після ≥ 1 с стабільного зв'язку з тумблером у DISARM і відсічка у FAILSAFE"
layer: "domain"
deps: ["T3", "T5"]
blocks: ["T9", "T11"]
acs: ["AC-08", "AC-09"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "test/host/test_drive.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T6 — Розширити drive_ctl_step: вихід з FAILSAFE після ≥ 1 с стабільного зв'язку з тумблером у DISARM і відсічка у FAILSAFE

## Place in the sequence

- **Blocked by:** T3 — Додати в app_state причину FAILSAFE, підвищення до «напруга» і охорону переходу FAILSAFE → DISARMED, T5 — Розширити drive_ctl_step: озброєння фронтом тумблера з умовами, рух стіком, роззброєння тумблером і з консолі · **Blocks:** T9 — Підключити знімок приймача в drive_hal, причину FAILSAFE, журнал подій зв'язку і crsf_start у main, T11 — Прогнати хостовий раннер і перевірки конституції для phase3-crsf, закрити покриття 49 пар і 126 пар · **Wave:** 4.
- **Lane:** спільні файли з T4, T5, T11 — серіалізовано.

## Why (user story)

> **As a** Розробник
> **I want** щоб після відновлення зв'язку ровер виходив з FAILSAFE у DISARMED, коли тумблер у DISARM
> **So that** мені не треба йти до ровера й скидати плату після кожного короткого зникнення зв'язку
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Ровер сам повертається в безпечний DISARMED після короткого зникнення зв'язку, але не після відсічки.

## Inlined context

> Відсічка по напрузі у FAILSAFE з причиною «втрата зв'язку»: `drive_ctl_step` перевіряє ту саму умову відсічки, що й в ARMED (той самий знімок `battery`, той самий поріг і правило застарілого виміру), і повідомляє `app_state` про причину «напруга» (ADR-0002).
>
> — `sad.md §4, журнал припущень, verbatim` · full text: [sad.md](../sad.md)

> CT->>CT: база тумблера невідома, перший кадр лише її встановлює → CT->>CT: зв'язок стабільний 1 с і тумблер у DISARM → CT->>ST: запит DISARMED → ST->>ST: причина втрата зв'язку, перехід дозволено
>
> — `sad.md §6, Critical flow 2, abridged` · full text: [sad.md](../sad.md)

> | Вихід із FAILSAFE | ≥ 1000 мс безперервно живого зв'язку + тумблер у DISARM; лише для причини «втрата зв'язку» (спрацювання відсічки по напрузі у FAILSAFE назавжди замінює причину на «напруга») |
>
> — `spec.md §6, «Вихід із FAILSAFE», verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. Правило безпеки «після відсічки — лише скидання» тоді захищене в одному місці, яке бачать усі викликачі переходів, і покривається тестами таблиці автомата.
>
> — `adr/0002, Decision outcome, abridged` · full text: [adr/0002-keep-failsafe-cause-in-state-machine-and-guard-recovery-there.md](../adr/0002-keep-failsafe-cause-in-state-machine-and-guard-recovery-there.md)

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

## Checklist

- [ ] У FAILSAFE: момент початку стабільного зв'язку = мітка першого кадру після втрати; скидається лише новою втратою (> 500 мс).
- [ ] Коли `now − початок ≥ 1000` і тумблер у DISARM (за базою, не «невідомо») → запит DISARMED; рішення про дозвіл за причиною лишається за `app_state` (T3).
- [ ] У FAILSAFE через зв'язок перевіряти умову відсічки; спрацювала → повідомити причину «напруга».
- [ ] Тести: 999 мс — ні, 1000 мс — так; втрата на 600 мс — вікно з нуля; тумблер ARM → чекає, DISARM при повному вікні → одразу; відсічка у FAILSAFE → запит DISARMED відхилено автоматом.

## Edge cases

| Case | Behaviour |
|---|---|
| Пропущено кілька кадрів, проміжок < 500 мс | вікно не скидається |
| Зв'язок живий ≥ 1 с, тумблер щойно опущений | вихід на цьому такті |
| Тумблер «невідомо» (перший кадр після втрати) | чекає наступного знімка |

## Definition of Done

- [ ] Хостові тести виходу з FAILSAFE зелені
- [ ] `idf.py build` без нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
