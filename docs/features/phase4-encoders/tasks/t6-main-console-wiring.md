---
id: T6
title: "Підключити encoders_start у main і колбеки enc_snapshot, enc_reset, enc_status у консоль"
layer: "wiring"
deps: ["T4", "T5"]
blocks: ["T7", "T8"]
acs: ["AC-03", "AC-08", "AC-09"]
files_hint: ["main/main.c", "components/rover_console/console_hal.c", "components/rover_console/include/console.h", "main/CMakeLists.txt"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "M"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T6 — Підключити encoders_start у main і колбеки enc_snapshot, enc_reset, enc_status у консоль

## Place in the sequence

- **Blocked by:** T4 — Додати в console_core команду enc: дві форми, хибні аргументи, дозвіл у всіх станах, рядок help, T5 — Створити encoders_hal: 4 блоки лічильника з watch-point без callback, фільтр 1000 нс, задача 20 мс, стан під portMUX · **Blocks:** T7 — Додати check_calib.sh (константи енкодерів проти measured.md) і прогнати всі хостові запобіжники, T8 — Написати acceptance.md: процедура приймання фази 4 на залізі для Розробника · **Wave:** 5.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** бачити швидкість кожного колеса, поки ровер їде з пульта чи з консолі
> **So that** я бачу розкид між колесами й бортами числами і можу підготувати колеса до фази 5
>
> — `spec.md §4, US-02, verbatim` · full text: [spec.md](../spec.md)

Робить знімок енкодерів доступним з консолі в усіх станах ровера.

## Inlined context

> Консоль: нові колбеки `console_ops_t`: `enc_snapshot(buf, n)` і `enc_reset(buf, n)` — текст відповіді форматує `encoders_core`; рядок у `status` — «енкодери: N з 4 доступні[, недоступні: …]» через `enc_status(buf, n)`.
> `main`: `encoders_start()` після `watchdog_start()` і до `battery_start()` — незалежно від того, чи стартує `control`; збій — рядок у журнал, ровер працює далі.
>
> — `sad.md §4, журнал припущень, abridged` · full text: [sad.md](../sad.md)

> Колбеки між компонентами передає `main` (`console_ops_t` фаз 0–3); дані між задачами — знімок під `portMUX`.
>
> — `sad.md §2, Conventions, abridged` · full text: [sad.md](../sad.md)

> **Chosen:** варіант 1 (рішення власника, 2026-10-19). Енкодери стають самостійною підсистемою: рахують і показуються, навіть якщо `control` не запустилася, а їх збій нічого не ламає в керуванні (spec AC-08).
>
> — `adr/0001, Decision outcome, abridged` · full text: [0001-sample-encoders-in-a-dedicated-20ms-task-and-publish-a-snapshot.md](../adr/0001-sample-encoders-in-a-dedicated-20ms-task-and-publish-a-snapshot.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Консольна команда `enc` / `enc reset` і поле `status` «енкодери» — див. витяг в Inlined context.

— `contracts/cli.md, «Нова команда enc», abridged` · full text: [cli.md](../contracts/cli.md)

## Acceptance criteria

### AC-03 (US-02) — cross-context

> **Given** ровер озброєний (з пульта або з консолі в стендовому режимі), колеса в повітрі
> **When** Розробник дає команду руху назад, а потім розвороту праворуч на місці
> **Then** при русі назад усі чотири швидкості від'ємні; при розвороті праворуч швидкості лівого борту (FL, RL) додатні, правого (FR, RR) від'ємні — знак кожного колеса збігається з командою, яку мікшер фази 1 дав його борту; енкодери рахують у кожному стані автомата, зокрема в DISARMED і FAILSAFE, коли колесо крутять рукою
>
> — `spec.md §5, AC-03, verbatim` · full text: [spec.md](../spec.md)

### AC-08 (US-05) — error

> **Given** при старті плати хоча б один енкодер не вдалося налаштувати (збій ініціалізації лічильника чи фільтра завад)
> **When** плата завершує старт, а Розробник показує стан, знімок енкодерів і озброює ровер
> **Then** недоступним вважається лише колесо, енкодер якого не налаштувався: журнал старту називає це колесо і причину збою, показ стану — скільки енкодерів доступно і які колеса недоступні (без причини), а у знімку енкодерів рядок такого колеса пише недоступність і причину замість значень (жодних хибних нулів, що виглядають як виміряні), тоді як решта коліс показують свої значення як звичайно; обнулення лічильників обнуляє доступні колеса і нагадує, які недоступні; ровер не перезавантажується, озброюється й їздить як у фазі 3, бо в цій фазі енкодери не беруть участі в керуванні. Викликати такий збій на залізі нічим, тож цей критерій приймається хостовим тестом логіки показу й оглядом коду, а не на залізі
>
> — `spec.md §5, AC-08, verbatim` · full text: [spec.md](../spec.md)

### AC-09 (US-02) — domain invariant

> **Given** ровер у будь-якому стані, зокрема в ARMED під час руху з пульта
> **When** Розробник обнуляє лічильники командою енкодерів
> **Then** лічильники й пройдена відстань усіх чотирьох коліс стають нулем одночасно, а консоль це підтверджує; швидкість не стрибає (вона рахується з різниці за інтервал), стан автомата, озброєння й команди бортам не змінюються; обнулити лічильники можна лише з консолі — пульт такої дії не має, а жодна консольна команда не змінює виміряні величини (імпульси на оберт, знак коліс, діаметр)
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Додати три колбеки в `console_ops_t` — `components/rover_console/include/console.h`
- [ ] Викликати колбеки на `enc`, `enc reset` і в `status`; NULL-колбек — «енкодери не підключені» — `console_hal.c`
- [ ] Викликати `encoders_start()` після `watchdog_start()` і передати колбеки — `main/main.c`
- [ ] Додати `encoders` у залежності `main` — `main/CMakeLists.txt` (якщо потрібно)

## Edge cases

| Case | Behaviour |
|---|---|
| `encoders_start` повернув помилку | старт продовжується; `enc` показує всі колеса недоступними з причиною |
| `control` не стартувала | `enc` працює, значення оновлюються |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень; `test/host/run_tests.sh` зелений; на залізі `enc` і `enc reset` відповідають (T8)
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (для C: збірка без нових попереджень і хостові скрипти)
