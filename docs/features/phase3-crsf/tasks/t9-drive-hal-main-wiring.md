---
id: T9
title: "Підключити знімок приймача в drive_hal, причину FAILSAFE, журнал подій зв'язку і crsf_start у main"
layer: "wiring"
deps: ["T6", "T7"]
blocks: ["T10", "T12"]
acs: ["AC-03", "AC-04", "AC-05", "AC-07", "AC-14"]
files_hint: ["components/drive/drive_hal.c", "components/drive/include/drive_hal.h", "components/drive/CMakeLists.txt", "main/main.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "M"
context_budget: "M"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T9 — Підключити знімок приймача в drive_hal, причину FAILSAFE, журнал подій зв'язку і crsf_start у main

## Place in the sequence

- **Blocked by:** T6 — Розширити drive_ctl_step: вихід з FAILSAFE після ≥ 1 с стабільного зв'язку з тумблером у DISARM і відсічка у FAILSAFE, T7 — Створити crsf_hal: UART2 420000 8N1, задача crsf_rx, знімок під portMUX, Task WDT · **Blocks:** T10 — Оновити консоль: help для arm і drive, відповіді disarm, рядки status про пульт через колбек rc_status, T12 — Підготувати чекліст приймання фази 3 на залізі і місця для записів у журнал bring-up · **Wave:** 5.
- **Lane:** спільні файли з T10 — серіалізовано.

## Why (user story)

> **As a** Розробник
> **I want** щоб ровер зупинявся й переходив у FAILSAFE, коли пульт вимкнено або зв'язок зник
> **So that** ровер ніколи не їде без мого керування довше ніж 0.5 с і один такт керування
>
> — `spec.md §4, US-03, verbatim` · full text: [spec.md](../spec.md)

Задача `control` на кожному такті читає знімок і застосовує рішення кроку на залізі.

## Inlined context

> `main`: `crsf_start()` після `drive_start()` і до консолі; невдача ініціалізації UART — рядок у журнал, ровер працює в стендовому режимі (кадрів не буде).
>
> — `sad.md §4, журнал припущень, verbatim` · full text: [sad.md](../sad.md)

> | Відхилений фронт тумблера | один рядок на фронт: першу невиконану умову (зв'язок → напруга → нейтраль → фронт) |
> | Втрата зв'язку в ARMED | перехід у FAILSAFE, причину, мітку останнього кадру і мітку переходу |
> | Втрата / відновлення зв'язку в DISARMED | один рядок на подію |
> | Поява пульта в стенді | що з'явився пульт, стенд закінчено, (якщо було ARMED) ровер роззброєно |
>
> — `contracts/cli.md, Журнал без команд, abridged` · full text: [contracts/cli.md](../contracts/cli.md)

> Порядок у control_task: apply_outputs (спершу ШІМ і напрямки, потім STBY) → app_state_request → статус → відповідь.
>
> — `звіт explorer, drive_hal.c:80-99, 248, abridged` · full text: [sad.md](../sad.md)

> **Chosen:** варіант 1. Він зберігає одного писача стану й виходів і порядок ADR-0002 фази 1, а вся логіка зв'язку стає частиною чистого кроку `drive_ctl_step` з тестами на підставному годиннику.
>
> — `adr/0001, Decision outcome, abridged` · full text: [adr/0001-decide-link-loss-in-control-task-from-receiver-snapshot.md](../adr/0001-decide-link-loss-in-control-task-from-receiver-snapshot.md)

> **Hard rule:** STBY піднімається тільки в стані ARMED і тільки свідомо; втрата кадрів CRSF довша за 500 мс → ШІМ у нуль, STBY вниз; ШІМ обмежений 0.75 до завершення фази 5; VBAT < 6.0 В → мотори не запускаються.
>
> — `constitution.md, п. 7, abridged` · full text: [../../constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Консольний контракт `contracts/cli.md`: цей таск реалізує рядки, наведені вище в Inlined context (класи відповіді / журнал / status); інших змін поверхні немає.

— `contracts/cli.md, abridged` · full text: [contracts/cli.md](../contracts/cli.md)

## Acceptance criteria

### AC-03 — happy path

> **Given** ровер у DISARMED, зв'язок живий, тумблер у DISARM, обидва осі правого стіка в нейтралі, напруга не нижче порога
> **When** Розробник перемикає тумблер у ARM, проїжджає, а тоді повертає тумблер у DISARM
> **Then** ровер переходить в ARMED (з сигналом зумера, фарами й задніми ліхтарями фази 2), консоль називає джерело озброєння «пульт»; після повернення в DISARM мотори зупиняються, STBY опускається, ровер переходить у DISARMED з сигналом зумера
>
> — `spec.md §5, AC-03, verbatim` · full text: [spec.md](../spec.md)

### AC-04 — domain invariant

> **Given** ровер у DISARMED, а одна з умов озброєння не виконана: стік не в нейтралі, напруга нижче порога, зв'язку немає, або тумблер уже стояв у ARM (на старті, у момент появи зв'язку — зокрема кожного відновлення після втрати, і в DISARMED, і у FAILSAFE — чи після роззброєння з консолі); на першому кадрі після старту чи після втрати попереднє положення тумблера вважається невідомим, тож цей кадр лише запам'ятовує положення і фронтом не є
> **When** Розробник перемикає тумблер у ARM (або тумблер просто лишається в ARM)
> **Then** ровер лишається в DISARMED, STBY не піднімається, консоль один раз на кожен відхилений фронт тумблера (не на кожному такті) пише рядок із першою невиконаною умовою простими словами, перевіряючи їх у порядку: зв'язок → напруга → нейтраль стіків → фронт тумблера, а показ стану називає умову, яка блокує озброєння зараз; щоб озброїтися, Розробник має усунути причину, повернути тумблер у DISARM і знову перемкнути в ARM — повернення стіка в нейтраль при тумблері в ARM ровер не озброює
>
> — `spec.md §5, AC-04, verbatim` · full text: [spec.md](../spec.md)

### AC-05 — happy path

> **Given** ровер озброєний з пульта й їде (колеса в повітрі)
> **When** Розробник вимикає пульт (або приймач перестає отримувати сигнал)
> **Then** не пізніше ніж через 0.52 с після останнього валідного кадру каналів обидва борти отримують нуль, STBY опускається, автомат переходить у FAILSAFE, консоль називає причину «втрата зв'язку» і час без кадрів; індикація така сама, як у будь-якому FAILSAFE фази 2: задні ліхтарі блимають червоним, індикатор плати червоний, зумер періодично пищить
>
> — `spec.md §5, AC-05, verbatim` · full text: [spec.md](../spec.md)

### AC-07 — cross-context

> **Given** ровер у DISARMED, зв'язок з пультом був живий
> **When** зв'язок втрачається на будь-який час
> **Then** ровер лишається в DISARMED (не переходить у FAILSAFE, бо мотори вже вимкнені й STBY внизу), консоль показує «зв'язку немає», озброєння з пульта відхиляється з цієї причини (AC-04), а консольне озброєння теж відхиляється, бо стендовий режим уже закінчився (AC-15); після відновлення зв'язку озброїтися можна за звичайних умов AC-03, причому тумблер, що стояв у ARM у момент відновлення, ровер не озброює (AC-04); для живого зв'язку при озброєнні достатньо одного валідного кадру за останні 0.5 с
>
> — `spec.md §5, AC-07, verbatim` · full text: [spec.md](../spec.md)

### AC-14 — cross-context

> **Given** стендовий режим, ровер озброєний з консолі (з рухом або без)
> **When** приходить перший валідний кадр каналів (Розробник увімкнув пульт); для кінця стендового режиму достатньо одного такого кадру
> **Then** мотори зупиняються, STBY опускається, ровер переходить у DISARMED, консоль пояснює, що з'явився пульт і керування перейшло до нього; стендовий режим закінчується до перезапуску плати, а озброїтися тепер можна лише тумблером за AC-03 (тумблер, що вже стоїть у ARM, треба спершу повернути в DISARM)
>
> — `spec.md §5, AC-14, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `drive_hal.c`: на кожному такті `crsf_snapshot_get` → поле `rc` входу кроку; `now_ms` з `esp_timer_get_time()`.
- [ ] Запит FAILSAFE із причиною — через нову функцію `app_state` (T3); порядок «виходи, потім стан» не змінювати.
- [ ] Журнал за ознаками кроку: відхилений фронт, втрата/відновлення зв'язку, поява пульта, вихід із FAILSAFE, підвищення причини — з мітками часу.
- [ ] Розширити `drive_status_t` полями для status: джерело керування, стан зв'язку, тяга/поворот, тумблер, що блокує озброєння.
- [ ] `main/main.c`: `crsf_start()` після `drive_start()`, до `console_start`; залежність `crsf` у `components/drive/CMakeLists.txt`.

## Edge cases

| Case | Behaviour |
|---|---|
| crsf не стартував | знімок без кадрів → ровер назавжди в стенді |
| Такт `control` затримався | рішення за `now_ms`, тож запізнення лише зсуває зупинку в межах 520 мс |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень
- [ ] Приймання на залізі в T12 (зупинка ≤ 520 мс, журнал подій)
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
