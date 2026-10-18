---
id: T8
title: "Перевірити ui_core на виході FAILSAFE → DISARMED: блимання, писк і індикатор гаснуть, сигнал переходу звучить"
layer: "tests"
deps: []
blocks: ["T11"]
acs: ["AC-08"]
files_hint: ["components/ui/ui_core.c", "test/host/test_ui.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T8 — Перевірити ui_core на виході FAILSAFE → DISARMED: блимання, писк і індикатор гаснуть, сигнал переходу звучить

## Place in the sequence

- **Blocked by:** — · **Blocks:** T11 — Прогнати хостовий раннер і перевірки конституції для phase3-crsf, закрити покриття 49 пар і 126 пар · **Wave:** 1.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** щоб після відновлення зв'язку ровер виходив з FAILSAFE у DISARMED, коли тумблер у DISARM
> **So that** мені не треба йти до ровера й скидати плату після кожного короткого зникнення зв'язку
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Індикація фази 2 правильно реагує на перехід, якого до фази 3 не існувало.

## Inlined context

> `ui` не змінюється: вихід із FAILSAFE у DISARMED він побачить опитуванням стану і подасть сигнал переходу в DISARMED; припинення блимання й писку перевіряє хостовий тест `ui_core` на переході, якого досі не було (§11).
>
> — `sad.md §4, журнал припущень, verbatim` · full text: [sad.md](../sad.md)

> | Вихід із FAILSAFE у DISARMED раніше ніколи не виконувався: `ui` може не погасити блимання чи періодичний писк | Medium | Хостовий тест `ui_core` на переході FAILSAFE → DISARMED; приймання на залізі (AC-08) | Агент |
>
> — `sad.md §11, verbatim` · full text: [sad.md](../sad.md)

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

## Checklist

- [ ] Тест у `test/host/test_ui.c`: такти ui_core у FAILSAFE (блимання, червоний індикатор, періодичний писк), потім DISARMED → задні ліхтарі червоні постійно, індикатор вимкнено, періодичний писк припинено, один сигнал переходу.
- [ ] Якщо тест червоний — мінімальна правка `components/ui/ui_core.c` (скидання відліків FAILSAFE на виході).

## Edge cases

| Case | Behaviour |
|---|---|
| Вихід із FAILSAFE посеред звучання періодичного сигналу | звучання припиняється, далі один сигнал переходу |
| Повторний вхід у FAILSAFE | відлік блимання й писку з нуля |

## Definition of Done

- [ ] Тест виходу з FAILSAFE у `test_ui.c` зелений
- [ ] `idf.py build` без нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
