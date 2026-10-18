---
id: T11
title: "Реалізувати драйвер індикатора плати за результатом P8 (окремий ADR)"
layer: "infra"
deps: ["T7", "T8", "T9"]
blocks: ["T14"]
acs: ["AC-19", "AC-20"]
files_hint: ["components/ui/ui_hal.c", "components/board/include/board_pins.h", "docs/measured.md", "docs/features/phase2-io/adr/"]
owner: "Розробник (замір і ADR), Агент (код)"
estimate: "M"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T11 — Реалізувати драйвер індикатора плати за результатом P8 (окремий ADR)

## Place in the sequence

- **Blocked by:** T7 — Додати діагностичні тести P8 (індикатор плати) і P9 (зумер) у tools/bringup, T8 — Створити ui_hal: задача ui, мʼютекс m_ui, опитування стану й бортів, запис розширювача, T9 — Додати в ui_hal зумер: LEDC таймер 1 канал 2, низький рівень спокою, ручний і автоматичний звук · **Blocks:** T14 — Підготувати чекліст приймання фази 2 на залізі і запис у журнал bring-up · **Wave:** 6, за графом залежностей.
- **Lane:** ділить файли з T8, T9, T10, T14 — серіалізується.

## Why (user story)

> **As a** Розробник
> **I want** щоб у FAILSAFE задні ліхтарі блимали червоним, вбудований індикатор плати горів червоним, а зумер періодично пищав
> **So that** я помічаю відсічку чи аварійну зупинку, навіть якщо консоль не відкрита, а розширювач недоступний
>
> — `spec.md §4, US-08, verbatim` · full text: [spec.md](../spec.md)

Цей task дає частину цієї історії: реалізувати драйвер індикатора плати за результатом P8 (окремий ADR).

## Inlined context

> **Hard rule:** Величини з розділу 9 документа `02-FIRMWARE-CONTEXT.md` не вигадуються. Якщо потрібна константа, якої немає в `docs/measured.md` — зупинитись і назвати, якого заміру бракує.
>
> — `constitution.md, п. 3, abridged` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** Номери GPIO існують рівно в одному файлі: `components/board/include/board_pins.h`. Літерал номера піна будь-де ще — дефект, навіть якщо значення правильне.
>
> — `constitution.md, п. 2, verbatim` · full text: [constitution.md](../../../constitution.md)

> **ADR-0004 (порт із двома кольорами):** `ui_core` просить «вимкнено» або «червоний»; `ui_hal` має заглушку, а реальний драйвер обирається окремим ADR після виміру (діагностичний тест P8).
>
> — `adr/0004-*.md, Decision outcome, abridged` · full text: [adr](../adr/)

> **OQ-3.** … Default now: потрібні лише два стани («червоний» і «вимкнений»); номер лінії лише в єдиному файлі розпіновки.
>
> — `spec.md §8, OQ-3, abridged` · full text: [spec.md](../spec.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [cli.md](../contracts/cli.md) · [adr/](../adr/) · [constitution](../../../constitution.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-19 — happy path

> **Given** ровер у ARMED (їде або стоїть), розширювач доступний чи ні
> **When** ровер переходить у FAILSAFE (відсічка по напрузі або будь-яка інша причина переходу)
> **Then** мотори вимкнено до будь-якого звернення до індикатора, задні ліхтарі переходять із режиму руху на блимання червоним (якщо розширювач доступний), вбудований індикатор плати світить червоним, а зумер після короткого сигналу переходу періодично пищить; усе це триває, доки плату не скинуть апаратно; помилка чи відсутність індикатора не затримує зупинку моторів і не змінює стану автомата
>
> — `spec.md §5, AC-19, verbatim` · full text: [spec.md](../spec.md)

### AC-20 — domain invariant

> **Given** автомат у довільному з семи станів
> **When** ровер перебуває в цьому стані
> **Then** вбудований індикатор плати світить червоним лише у FAILSAFE і вимкнений в усіх інших станах, зокрема в DISARMED, ARMED і BOOT: червоний індикатор не може бути хибним сигналом тривоги
>
> — `spec.md §5, AC-20, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] Прочитати результат P8 у `docs/measured.md`.
- [x] Записати окремий ADR (наступний номер) з вибором драйвера на основі виміру; додати лінію індикатора в `board_pins.h` за вказівкою Розробника.
- [x] Реалізувати `ui_indicator_set(колір)` у `ui_hal`, замінивши заглушку; збій індикатора не затримує нічого й не змінює стану автомата.

## Edge cases

| Case | Behaviour |
|---|---|
| Тип світлодіода не збігся з припущенням | ADR пишеться за виміром, не за фото |
| Індикатор недоступний | FAILSAFE і зупинка моторів не страждають |

## Definition of Done

- [x] Драйвер відповідає ADR і виміру, `idf.py build` без нових попереджень.
- [x] Приймання на залізі: у FAILSAFE червоний індикатор, в інших станах вимкнений (AC-19, AC-20).
- [x] Hard rules вище виконуються.
- [x] lint + vet clean
