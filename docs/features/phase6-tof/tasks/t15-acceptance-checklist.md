---
id: T15
title: "Написати acceptance.md: процедура приймання фази 6 на залізі для Розробника і шаблон записів у журнал вимірювань"
layer: "docs"
deps: ["T13"]
blocks: []
acs: ["AC-19"]
files_hint: ["docs/features/phase6-tof/acceptance.md"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T15 — Написати acceptance.md: процедура приймання фази 6 на залізі для Розробника і шаблон записів у журнал вимірювань

## Place in the sequence

- **Blocked by:** T13 — Підключити в main старт tof і колбеки консолі tof_show та tof_reinit · **Blocks:** — · **Wave:** 7.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Агент
> **I want** чітко знати, що поріг перешкоди і приймання на залізі належать Розробникові
> **So that** я не вигадую поріг і не позначаю виконаним неперевірене
>
> — `spec.md §4, US-07, verbatim` · full text: [spec.md](../spec.md)

Ця задача дає Розробникові покрокову процедуру приймання на залізі та межу, за якою Агент не позначає задачу виконаною.

## Inlined context

> *Примітка до перевірки:* фільтр, сектори, обмеження тяги, відмову, перехід станів, таблицю з 12 пар, дозволи й розбір команд перевіряють хостові тести з підставним годинником. Зупинку перед перешкодою, відмову на ходу, повернення датчика, світло під час повторної ініціалізації й відсутність збоїв шини приймає Розробник на залізі, результат — у журнал bring-up (пункт 6 конституції).
>
> — `spec.md §5, «Примітка до перевірки», verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. Затримка шини лишається на core 0; `control` лише копіює знімок і за віком (> 250 мс або `valid == false`) розуміє, що сектори не охороняються.
>
> — `adr/0003-poll-rangefinders-in-a-separate-sensors-task-and-detect-failure-there.md, Decision outcome, abridged` · full text: [adr/0003-poll-rangefinders-in-a-separate-sensors-task-and-detect-failure-there.md](../adr/0003-poll-rangefinders-in-a-separate-sensors-task-and-detect-failure-there.md)

> | Зазор після зупинки в найгіршому випадку ≈ 42 мм, лише трохи вище 40 мм (зона без заміру) | Medium | Перевірка рулеткою при прийманні AC-01; поріг змінюється правкою однієї константи (запис у журналі вимірювань) | Розробник |
>
> — `sad.md §11, ризик «зазор після зупинки», verbatim` · full text: [sad.md](../sad.md)

> | Open architectural decision: поведінка далекоміра ближче 40 мм (spec OQ-2) | Open question | Resolve before приймання фази 6 на залізі; замір P4 режим 1 долонею на 10, 20, 30 мм | Розробник |
>
> — `sad.md §11, OQ-2, verbatim` · full text: [sad.md](../sad.md)

> **Hard rule:** 6. Кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`. Логіка живе в core і покривається тестами на хості. Для HAL тести на моках не пишуться: критерій приймання — перевірка на залізі, записана в журнал bring-up.
>
> — `constitution.md, п. 6, verbatim` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** 13. Результат кожної передфази записується в `docs/measured.md` до того, як будь-який код чи специфікація використає це значення. Незаписане число для пунктів 3 і 4 не існує.
>
> — `constitution.md, п. 13, verbatim` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-19 (US-07) — authorization

> **Given** задача фази 6 стосується поведінки на залізі (зупинка перед перешкодою, відмова далекоміра на ходу, повернення датчика, світло під час повторної ініціалізації) або вибору порогу
> **When** Агент закінчив код і хостовий прогін зелений
> **Then** Агент не позначає задачу виконаною, доки Розробник не запише приймання на залізі в журнал bring-up; Агент не підставляє поріг перешкоди (ні «типовий» з документа 02, ні вгаданий) і не записує число в журнал вимірювань від себе, не прошиває плату, не фіксує зміни в історії без прямої вказівки, не змінює захищені файли без явної згоди Розробника і не редагує каталог проєкту плати взагалі
>
> — `spec.md §5, AC-19, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Скласти кроки приймання: AC-01 (стіна, три швидкості, рулетка), AC-02 (коробка збоку), AC-03, AC-04, AC-05, AC-06 (вийняти кожен із чотирьох шлейфів), AC-07 (старт без датчиків), AC-08 (reinit і світло), AC-09, AC-10, AC-11, AC-12 (напруга/зв'язок/роззброєння), AC-14, AC-15, AC-16 (60 с їзди) — `docs/features/phase6-tof/acceptance.md`
- [ ] Додати замір OQ-2: долоня на 10, 20, 30 мм від кожного далекоміра (P4, режим 1) і шаблон запису в `docs/measured.md` — `docs/features/phase6-tof/acceptance.md`
- [ ] Додати запис зазору після зупинки (рулетка) і підсумок KPI з §7 — `docs/features/phase6-tof/acceptance.md`
- [ ] Вказати: числа записує Розробник; Агент не підставляє поріг, не прошиває, не фіксує зміни без вказівки — `docs/features/phase6-tof/acceptance.md`

## Edge cases

| Case | Behaviour |
|---|---|
| Зазор після зупинки менше 30 мм | Підняти поріг однією константою, записати в журнал вимірювань, повторити заїзд |
| Далекомір ближче 40 мм показує «далеко» (OQ-2) | Записати в журнал, повернутися до власника: захист там не діє |
| Хоч один крок не пройдено | Задача не позначається виконаною |

## Definition of Done

- [ ] `acceptance.md` покриває всі апаратні AC і KPI з §7
- [ ] Містить місце для запису результатів і дат кожного кроку
- [ ] every Hard Rule inlined above still holds
