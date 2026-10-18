---
id: T12
title: "Підготувати чекліст приймання фази 3 на залізі і місця для записів у журнал bring-up"
layer: "docs"
deps: ["T9", "T10", "T11"]
blocks: []
acs: ["AC-20"]
files_hint: ["docs/features/phase3-crsf/acceptance.md"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T12 — Підготувати чекліст приймання фази 3 на залізі і місця для записів у журнал bring-up

## Place in the sequence

- **Blocked by:** T9 — Підключити знімок приймача в drive_hal, причину FAILSAFE, журнал подій зв'язку і crsf_start у main, T10 — Оновити консоль: help для arm і drive, відповіді disarm, рядки status про пульт через колбек rc_status, T11 — Прогнати хостовий раннер і перевірки конституції для phase3-crsf, закрити покриття 49 пар і 126 пар · **Blocks:** — · **Wave:** 8.
- **Lane:** own lane.

## Why (user story)

> **As a** Агент
> **I want** знати, що приймання керування з пульта на залізі виконує лише Розробник
> **So that** я не відмічаю апаратну задачу виконаною на основі лише зеленого хостового прогону
>
> — `spec.md §4, US-09, verbatim` · full text: [spec.md](../spec.md)

Розробник отримує покроковий план перевірки на залізі; Агент не закриває апаратні задачі без його запису.

## Inlined context

> **Записи приймання на залізі в журналі bring-up** — baseline: 0, target: 8 записів до початку специфікації фази 4 (керування стіком у чотирьох напрямках і мертва зона; озброєння й роззброєння тумблером; відмови озброєння за кожною умовою; 10 вимкнень пульта на ходу з часом зупинки; вихід із FAILSAFE після відновлення зв'язку; стендовий режим і його закінчення при появі пульта; перепідключення приймача без зміни стану; 10 хв їзди без хибного FAILSAFE).
>
> — `spec.md §7, KPI 1, verbatim` · full text: [spec.md](../spec.md)

> - [ ] **OQ-2.** Чи налаштований приймач на режим failsafe «без імпульсів» (кадри припиняються), а не на «фіксовані положення»? … Розробник перевіряє налаштування приймача з пульта й записує його в `docs/measured.md`. — owner: Розробник, due: до приймання фази 3 на залізі
> - [ ] **OQ-3.** … приймання AC-17 на залізі — після закриття OQ-1 фази 1.
>
> — `spec.md §8, OQ-2 і OQ-3, abridged` · full text: [spec.md](../spec.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-20 — authorization

> **Given** задача фази 3 стосується приймача, керування з пульта, зупинки при втраті зв'язку чи виходу з FAILSAFE (апаратна частина)
> **When** Агент закінчив код і хостовий прогін зелений
> **Then** Агент не позначає задачу виконаною, доки Розробник не запише приймання на залізі в журнал bring-up; Агент не прошиває плату, не фіксує зміни в історії без прямої вказівки і не змінює захищені файли та каталог проєкту плати без явної згоди Розробника
>
> — `spec.md §5, AC-20, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Створити `docs/features/phase3-crsf/acceptance.md` за зразком фази 2: передумови (OQ-2, колеса в повітрі), по кроку на кожен із 8 записів KPI, очікуване й місце для результату.
- [ ] Додати кроки: вільна пам'ять ≥ 345000 байт; лічильники кадрів і 0 % битих; рядки status у трьох варіантах.

## Edge cases

| Case | Behaviour |
|---|---|
| OQ-2 не закрите | кроки AC-05/AC-06 не приймаються |
| OQ-3 не закрите | крок AC-17 не приймається |

## Definition of Done

- [ ] `acceptance.md` містить по кроку на кожен апаратний AC і запис KPI
- [ ] Агент не позначає T7, T9, T10 виконаними до запису Розробника
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
