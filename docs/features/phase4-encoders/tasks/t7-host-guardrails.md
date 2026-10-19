---
id: T7
title: "Додати check_calib.sh (константи енкодерів проти measured.md) і прогнати всі хостові запобіжники"
layer: "tests"
deps: ["T6"]
blocks: []
acs: ["AC-10", "AC-11"]
files_hint: ["test/host/check_calib.sh", "test/host/selftest_tools.sh"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T7 — Додати check_calib.sh (константи енкодерів проти measured.md) і прогнати всі хостові запобіжники

## Place in the sequence

- **Blocked by:** T6 — Підключити encoders_start у main і колбеки enc_snapshot, enc_reset, enc_status у консоль · **Blocks:** — · **Wave:** 6.
- **Lane:** own lane.

## Why (user story)

> **As a** Агент
> **I want** доводити накопичення лічильника через межу, знак, швидкість за інтервал, перерахунок в оберти й відстань і розбір команди тестами на хості
> **So that** логіка перевірена до того, як Розробник крутить колеса на залізі
>
> — `spec.md §4, US-06, verbatim` · full text: [spec.md](../spec.md)

Автоматично ловить константу, якої немає в журналі вимірювань, і закриває гейт фази.

## Inlined context

> Калібровані константи — лише з `docs/measured.md`, у `encoders_calib.h` з датою виміру.
>
> — `sad.md §8, «Калібровані константи», abridged` · full text: [sad.md](../sad.md)

> **Hard rule:** Величини з розділу 9 документа `02-FIRMWARE-CONTEXT.md` не вигадуються. Якщо потрібна константа, якої немає в `docs/measured.md` — зупинитись і назвати, якого заміру бракує.
>
> — `constitution.md, п. 3, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-10 (US-06) — happy path

> **Given** Агент завершив зміну в межах фази 4
> **When** Агент запускає збірку проєкту і прогін хостових тестів
> **Then** збірка завершується без помилок і без нових попереджень, а прогін повідомляє кількість тестів і що всі пройдені; серед них перевірено: накопичення лічильника через апаратну межу в обидва боки (зокрема багаторазовий перехід і перехід з одночасним рухом), застосування знака кожного колеса, швидкість як приріст, поділений на фактичний час між вибірками, з підставним годинником (зокрема нерівні інтервали, нуль не пізніше ніж через два інтервали після останнього імпульсу й відсутність стрибка після обнулення), показ недоступного колеса поруч із доступними (AC-08), перерахунок в оберти, лінійну швидкість і відстань за виміряним діаметром і розбір команди енкодерів з усіма відхиленнями
>
> — `spec.md §5, AC-10, verbatim` · full text: [spec.md](../spec.md)

### AC-11 (US-06) — error

> **Given** у зміні файл чистої логіки (з суфіксом core) підключив заголовок ESP-IDF чи FreeRTOS, або номер лінії енкодера з'явився літералом поза єдиним файлом розпіновки, або імпульси на оберт, знак колеса чи діаметр задані числом, якого немає в журналі вимірювань, або періодична задача чекає звичайною затримкою замість очікування до моменту, або з'явилося нове попередження збірки
> **When** Агент запускає збірку і хостові перевірки
> **Then** задачу не вважають завершеною, Агент називає файл і причину та не позначає задачу виконаною
>
> — `spec.md §5, AC-11, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Скрипт: значення `ENC_PULSES_PER_REV`, `ENC_DIR_SIGN`, `WHEEL_DIAMETER_MM` з `encoders_calib.h` мають бути в рядках відповідних констант `docs/measured.md`; інакше — назва константи й ненульовий код — `test/host/check_calib.sh`
- [ ] Самоперевірка скрипта на навмисно хибній копії — `test/host/selftest_tools.sh`
- [ ] Прогнати `run_tests.sh`, `check_core_headers.sh`, `check_pins.sh`, `check_delays.sh`, `check_protected.sh`, `check_calib.sh` і `idf.py build`

## Edge cases

| Case | Behaviour |
|---|---|
| константа є в calib, але її рядок у measured.md відсутній | скрипт падає з назвою константи |
| значення розходиться (наприклад, 43 замість 44) | скрипт падає з обома значеннями |

## Definition of Done

- [ ] усі хостові скрипти й `run_tests.sh` зелені, `selftest_tools.sh` ловить навмисне розходження; `idf.py build` без нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (для C: збірка без нових попереджень і хостові скрипти)
