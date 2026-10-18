---
id: T2
title: "Доповнити crsf_core: намір пілота (нормалізація, мертва зона, тумблер, функція фронту) і рядки status"
layer: "domain"
deps: ["T1"]
blocks: ["T5", "T10"]
acs: ["AC-02", "AC-11", "AC-12"]
files_hint: ["components/crsf/crsf_core.c", "components/crsf/include/crsf_core.h", "components/crsf/include/crsf_calib.h", "test/host/test_crsf.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "M"
context_budget: "M"
status: "done"
step: "COMMIT"
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T2 — Доповнити crsf_core: намір пілота (нормалізація, мертва зона, тумблер, функція фронту) і рядки status

## Place in the sequence

- **Blocked by:** T1 — Створити crsf_core: збирач кадрів із потоку байтів, CRC8, розпакування 16 каналів, лічильники · **Blocks:** T5 — Розширити drive_ctl_step: озброєння фронтом тумблера з умовами, рух стіком, роззброєння тумблером і з консолі, T10 — Оновити консоль: help для arm і drive, відповіді disarm, рядки status про пульт через колбек rc_status · **Wave:** 2.
- **Lane:** спільні файли з T1 — серіалізовано.

## Why (user story)

> **As a** Розробник
> **I want** у ARMED керувати тягою й поворотом правим стіком пульта
> **So that** ровер їде туди, куди я відхиляю стік, і зупиняється, коли я його відпускаю
>
> — `spec.md §4, US-01, verbatim` · full text: [spec.md](../spec.md)

Чисті функції, що перетворюють сирі канали на тягу, поворот і положення тумблера, плюс форматування рядків показу стану.

## Inlined context

> Нормалізація: x = (raw − 992) / 819, обмеження до [−1, 1]; мертва зона по кожній осі окремо d = 0.05: |x| ≤ d → 0, інакше знак(x) · (|x| − d) / (1 − d). Знаки осей — константи `CRSF_THROTTLE_SIGN = +1`, `CRSF_STEER_SIGN = +1` у `crsf_calib.h` з позначкою «виміряно: 2026-10-18» (spec OQ-1, `docs/measured.md`).
>
> — `sad.md §4, журнал припущень, verbatim` · full text: [sad.md](../sad.md)

> Тумблер: канал 5 (індекс 4) > 992 → ARM. Фронт = зміна з DISARM на ARM між двома послідовними знімками з новим кадром, які `control` бачить на своїх тактах 20 мс, за живого зв'язку (проміжні кадри між тактами не потрібні: положення тумблера фіксоване); база «невідома» на старті й після кожної втрати зв'язку, перший кадр лише встановлює базу.
>
> — `sad.md §4, журнал припущень, abridged` · full text: [sad.md](../sad.md)

> Показ стану: новий колбек `rc_status` у `console_ops_t`; рядки форматує `crsf_core` (чиста функція), дані — зі знімка `crsf` і статусу `drive` (джерело керування, причина FAILSAFE з `app_state`).
>
> — `sad.md §4, журнал припущень, verbatim` · full text: [sad.md](../sad.md)

> | джерело керування | `стенд` — увесь стендовий режим; `пульт` — режим «пульт» і зв'язок живий; `немає` — режим «пульт» і зв'язок втрачено |
> | час від останнього кадру каналів | мс; прочерк, поки кадрів не було |
> | сирі канали | значення каналів 1, 2 і 5 (останній валідний кадр; прочерк, поки кадрів не було) |
> | тумблер | ARM / DISARM / невідомо (база фронту невідома) |
>
> — `contracts/cli.md, `status`: нові поля, abridged` · full text: [contracts/cli.md](../contracts/cli.md)

> **Chosen:** варіант 1. Перетворення каналів лишається поруч із протоколом (і тестується разом із розбором), а рішення про те, чиїм командам вірити, — у тому самому чистому кроці, що вирішує про виходи й стан.
>
> — `adr/0003, Decision outcome, abridged` · full text: [adr/0003-turn-channels-into-pilot-intent-in-crsf-core-and-arbitrate-source-in-control-step.md](../adr/0003-turn-channels-into-pilot-intent-in-crsf-core-and-arbitrate-source-in-control-step.md)

> **Hard rule:** Кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`. Логіка живе в core і покривається тестами на хості.
>
> — `constitution.md, п. 6, abridged` · full text: [../../constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-02 — domain invariant

> **Given** ровер озброєний з пульта
> **When** Розробник відхиляє стік до упору (зокрема по діагоналі), або тримає стік у межах мертвої зони навколо центру
> **Then** жоден борт не отримує більше за обмеження потужності 0.75 (мікшер фази 1 нормалізує суму тяги й повороту); у межах мертвої зони обидва борти стоять, а за її межею швидкість росте плавно від нуля, без стрибка на краю зони
>
> — `spec.md §5, AC-02, verbatim` · full text: [spec.md](../spec.md)

### AC-11 — error

> **Given** зв'язок живий
> **When** валідний кадр каналів несе значення стіка чи тумблера поза робочим діапазоном пульта (нижче найменшого або вище найбільшого, зокрема нуль)
> **Then** значення обмежується краєм робочого діапазону, ніколи не дає команди бортам понад обмеження потужності, а тумблер із нулем трактується як DISARM
>
> — `spec.md §5, AC-11, verbatim` · full text: [spec.md](../spec.md)

### AC-12 — happy path

> **Given** плата запущена, Розробник відкрив консоль
> **When** Розробник викликає команду показу стану в стендовому режимі, із живим зв'язком і після втрати зв'язку
> **Then** консоль показує: джерело керування («стенд» увесь стендовий режим, незалежно від озброєння; «пульт», коли стендовий режим закінчився і зв'язок живий; «немає», коли стендовий режим закінчився, а зв'язок втрачено), стан зв'язку (не було від старту, живий, втрачено) і час від останнього валідного кадру каналів (прочерк, поки кадрів не було), лічильники прийнятих кадрів каналів, кадрів із неправильною контрольною сумою та з неправильною довжиною, поточні тягу й поворот після мертвої зони, сирі значення каналів 1, 2 і 5 (потрібні для заміру OQ-1 і перевірки межі тумблера), положення тумблера, а у FAILSAFE — причину аварійної зупинки; решта рядків фаз 1 і 2 лишаються
>
> — `spec.md §5, AC-12, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Створити `components/crsf/include/crsf_calib.h`: `CRSF_THROTTLE_SIGN +1`, `CRSF_STEER_SIGN +1`, коментар «виміряно: 2026-10-18, docs/measured.md, Фаза 3 OQ-1».
- [ ] `crsf_axis(raw, sign)` → нормалізація, обмеження, мертва зона 0.05 по осі, лінійно без стрибка; `crsf_switch_arm(raw)` → `raw > 992`.
- [ ] `crsf_edge(base, cur)` — чиста функція фронту з базою {невідомо, DISARM, ARM}; повертає (фронт DISARM→ARM так/ні, нова база). Сама база живе у `drive_ctl_t` (T5).
- [ ] `crsf_fmt_status(...)` — рядки джерела керування, зв'язку, часу від кадру (прочерк), лічильників, сирих CH1/CH2/CH5, тяги й повороту, тумблера, «що блокує озброєння», причини FAILSAFE.
- [ ] Тести в `test/host/test_crsf.c`: 992 → 0; межа мертвої зони (неперервність на |x| = 0.05); 1808 → ≈ +0.98; 172 → −1; 0 → тумблер DISARM; 993 → ARM, 992 → DISARM; база невідомо → перший кадр не фронт; рядки status для трьох варіантів джерела.

## Edge cases

| Case | Behaviour |
|---|---|
| Сире значення рівно на краю мертвої зони | нуль; на крок далі — мале значення того самого знака, без стрибка |
| Тумблер з нулем у кадрі | DISARM (AC-11) |
| База невідома, перший кадр з ARM | фронту немає, база = ARM |
| Кадрів ще не було | у рядку status час і сирі канали — прочерк |

## Definition of Done

- [ ] Хостові тести наміру й рядків status зелені
- [ ] `crsf_calib.h` посилається на запис у `docs/measured.md`
- [ ] `check_core_headers.sh` чистий
- [ ] `idf.py build` без нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
