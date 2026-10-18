---
id: T10
title: "Зафіксувати частоту автоматичних сигналів у ui_calib.h за результатом P9"
layer: "docs"
deps: ["T7", "T9"]
blocks: ["T14"]
acs: ["AC-21", "AC-22"]
files_hint: ["components/ui/include/ui_calib.h", "docs/measured.md"]
owner: "Розробник (вибір частоти), Агент (константа)"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T10 — Зафіксувати частоту автоматичних сигналів у ui_calib.h за результатом P9

## Place in the sequence

- **Blocked by:** T7 — Додати діагностичні тести P8 (індикатор плати) і P9 (зумер) у tools/bringup, T9 — Додати в ui_hal зумер: LEDC таймер 1 канал 2, низький рівень спокою, ручний і автоматичний звук · **Blocks:** T14 — Підготувати чекліст приймання фази 2 на залізі і запис у журнал bring-up · **Wave:** 6, за графом залежностей.
- **Lane:** ділить файли з T11, T14 — серіалізується.

## Why (user story)

> **As a** Розробник
> **I want** щоб зумер подавав короткий сигнал, коли ровер переходить у DISARMED, ARMED чи FAILSAFE
> **So that** я чую озброєння, роззброєння (зокрема автоматичне після спливання команди руху) і аварійну зупинку, не дивлячись у консоль
>
> — `spec.md §4, US-09, verbatim` · full text: [spec.md](../spec.md)

Цей task дає частину цієї історії: зафіксувати частоту автоматичних сигналів у ui_calib.h за результатом P9.

## Inlined context

> **Hard rule:** Величини з розділу 9 документа `02-FIRMWARE-CONTEXT.md` не вигадуються. Якщо потрібна константа, якої немає в `docs/measured.md` — зупинитись і назвати, якого заміру бракує.
>
> — `constitution.md, п. 3, abridged` · full text: [constitution.md](../../../constitution.md)

> **OQ-2.** Які частоти зумера реально гучні? … Default now: діапазон 200–5000 Гц; частоти автоматичних сигналів (AC-21, AC-22) не вигадуються: Розробник під час перевірки записує у `docs/measured.md`, на яких частотах зумер чути найкраще, і обирає з них частоту сигналів.
>
> — `spec.md §8, OQ-2, abridged` · full text: [spec.md](../spec.md)

> Частота сигналів переходу і FAILSAFE — константа в `ui_calib.h` із позначкою «виміряно: дата»; доки OQ-2 відкрите, константи немає, і код сигналів не пишеться.
>
> — `sad.md §4, §4 ledger, abridged` · full text: [sad.md](../sad.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [cli.md](../contracts/cli.md) · [adr/](../adr/) · [constitution](../../../constitution.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-21 — happy path

> **Given** ровер у BOOT, SELFTEST, DISARMED або ARMED, консоль активна чи ні
> **When** автомат переходить у DISARMED (зокрема на старті, після `disarm` чи після спливання команди руху), у ARMED або у FAILSAFE
> **Then** зумер подає один короткий сигнал (тривалість і частота в §6); перехід через проміжний стан на старті (BOOT → SELFTEST → DISARMED) дає один сигнал, а не по одному на кожен крок; сигнал подається і тоді, коли розширювач недоступний; ручне звучання, що триває в цей момент, замінюється сигналом переходу; ручна команда зумера, подана під час сигналу переходу, його не перериває і починає звучати одразу після його закінчення; сигнал подається лише коли попередній стан відмінний від нового: `disarm` у DISARMED чи FAILSAFE, відхилене озброєння і повторний запит того самого стану сигналу не дають
>
> — `spec.md §5, AC-21, verbatim` · full text: [spec.md](../spec.md)

### AC-22 — happy path

> **Given** ровер у FAILSAFE
> **When** спливає період повтору сигналу
> **Then** зумер знову подає короткий сигнал (перший періодичний сигнал — через період повтору після входу у FAILSAFE, а не відразу) і повторює його з тим самим періодом, доки плату не скинуть апаратно; команда зумера в FAILSAFE, як і раніше, відхиляється (AC-06); сигнал не залежить від розширювача й не блокує консоль
>
> — `spec.md §5, AC-22, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] Прочитати у `docs/measured.md` результат P9 (частоти, що чути найкраще) і вибір Розробника.
- [x] Додати `ui_calib.h` з константою частоти й позначкою «виміряно: дата».
- [x] Підключити константу в `ui_hal` як параметр сигналів `ui_core`.

## Edge cases

| Case | Behaviour |
|---|---|
| Результату P9 у `docs/measured.md` немає | задачу не починати, назвати бракуючий замір |

## Definition of Done

- [x] Константа взята з `docs/measured.md`, не вигадана.
- [x] `idf.py build` без помилок і нових попереджень.
- [x] Hard rules вище виконуються.
- [x] lint + vet clean
