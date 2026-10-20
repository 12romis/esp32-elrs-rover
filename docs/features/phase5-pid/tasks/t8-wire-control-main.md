---
id: T8
title: "Підключити регулятор: drive_hal подає знімок і набір, main стартує pid_hal і pid_trace, колбеки консолі"
layer: "wiring"
deps: ["T5", "T6", "T7"]
blocks: ["T9", "T10"]
acs: ["AC-03", "AC-05", "AC-06"]
files_hint: ["components/drive/drive_hal.c", "components/drive/CMakeLists.txt", "main/main.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "M"
context_budget: "M"
status: "review"
step: "REVIEW — гейт ✓; чекає приймання Розробника на залізі (spec AC-20)"
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T8 — Підключити регулятор: drive_hal подає знімок і набір, main стартує pid_hal і pid_trace, колбеки консолі

## Place in the sequence

- **Blocked by:** T5 — Додати в drive_ctl_step відкати і зупинки: недоступні енкодери, застарілий знімок, скидання при зупинках, відмова озброєння під час збереження · T6 — Додати в console_core команду pid: форми, відмови, матриця дозволів за станом · T7 — Створити pid_hal: набір під portMUX, завантаження й збереження в NVS, задача pid_trace · **Blocks:** T9 — Посилити хостові перевірки: межа потужності 750, core без ESP-IDF, повний прогін з test_pid.c · T10 — Написати acceptance.md: процедура приймання фази 5 на залізі для Розробника · **Wave:** 6 — збирає T5, T6, T7 разом.
- **Lane:** shares `components/drive/CMakeLists.txt` with T4 — serialized.

## Why (user story)

> **As a** Розробник
> **I want** змінювати коефіцієнти регулятора командами консолі на ходу, вмикати й вимикати регулятор і бачити трасування цілі, виміру й виходу кожного борту
> **So that** я підбираю набір коефіцієнтів на залізі без перепрошивки
>
> — `spec.md §4, US-03, verbatim` · full text: [spec.md](../spec.md)

Робить регулятор досяжним з пульта й консолі: збирає ядро, HAL і команди в робочу прошивку.

## Inlined context

> Задачі control (20 мс, prio 5, core 1; drive_ctl_step — чистий крок у drive_core), encoders (20 мс, prio 4, core 1; знімок enc_state_t під portMUX через encoders_state_get). control стартує до storage_init.
>
> — `sad.md §3, brownfield-нотатка, abridged` · full text: [sad.md](../sad.md)

> Старт: `pid_hal_init()` у `main` після `storage_init()` і до старту `control`: читає запис, `pid_core` перевіряє магію, версію, розмір, CRC32, скінченність і діапазони; невдача — «регулятор не налаштований» і рядок журналу з причиною (spec AC-07).
>
> — `sad.md §4, журнал припущень «Старт», verbatim` · full text: [sad.md](../sad.md)

> Журнал: одна подія — один рядок (відкат на режим без регулятора, завантаження набору при старті); без рядків на кожен такт поза трасуванням.
>
> — `sad.md §4, журнал припущень «Журнал», verbatim` · full text: [sad.md](../sad.md)

> **Chosen:** варіант 1. Він лишає одного писача виходів і поєднує регулятор зі зупинками в одному чистому кроці, який тестується на хості; не змінює відвантажену фазу 4 і такт `control` на `vTaskDelayUntil`. Щоб не рахувати той самий знімок двічі, P, I і D оновлюються лише на новому інтервалі енкодерів з dt з їхніх міток; нульова ціль і зупинки діють у кожному такті. Варіант 2 переробляє фазу 4 і вимагає повторного приймання енкодерів.
>
> — `adr/0001, Decision outcome, verbatim` · full text: [adr/0001-run-speed-regulator-inside-control-step-from-encoder-snapshot.md](../adr/0001-run-speed-regulator-inside-control-step-from-encoder-snapshot.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface (консоль описано в spec §5).

## Acceptance criteria

### AC-03 (US-03) — happy path

> **Given** ровер у будь-якому стані автомата
> **When** Розробник змінює один коефіцієнт набору командою регулятора з числом (наприклад, Kp на 1.2), а потім показує стан регулятора
> **Then** консоль підтверджує зміну зі старим і новим значенням і нагадує, що набір не збережено; у ARMED нове значення діє з наступного такту керування (трасування показує зміну реакції без паузи); показ стану регулятора називає режим (увімкнений, вимкнений або не налаштований), усі чотири числа набору з позначкою, чи збережено їх, і для кожного борту задану швидкість, швидкість борту, вихід регулятора та режим борту (за двома колесами, за одним колесом із назвою недоступного, без регулятора); стан автомата, озброєння і команди бортам не змінюються самою командою
>
> — `spec.md §5, AC-03, verbatim` · full text: [spec.md](../spec.md)

### AC-05 (US-03) — happy path

> **Given** ровер у DISARMED, регулятор увімкнено
> **When** Розробник вмикає трасування регулятора, озброює ровер, їздить 10 с, роззброює і вимикає трасування
> **Then** поки ровер озброєний, консоль виводить рядок трасування 10 разів на секунду (за 10 с — 100 ± 2 рядки), кожен із часом від старту плати і для кожного борту задану швидкість, швидкість борту і вихід регулятора; поза ARMED рядки трасування не виводяться; після вимкнення трасування рядків немає в жодному стані; затримка «кадр пульта → борти» з увімкненим трасуванням не гірша за §6
>
> — `spec.md §5, AC-05, verbatim` · full text: [spec.md](../spec.md)

### AC-06 (US-04) — happy path

> **Given** ровер у DISARMED, Розробник задав усі чотири числа набору й увімкнув регулятор
> **When** Розробник зберігає набір командою збереження і перезапускає плату
> **Then** консоль підтверджує збереження; після старту журнал старту і показ стану регулятора називають збережений набір і режим «увімкнений», а позначка «не збережено» зникла; у наступному озброєнні регулятор працює з цим набором без жодних команд. Якщо набір збережено з вимкненим регулятором, після перезапуску показ стану називає збережений набір і режим «вимкнений», а ровер їздить у режимі без регулятора, доки Розробник не ввімкне його в DISARMED. Збереження неповного набору відхиляється з поясненням, які числа ще не задані, і запис у памʼяті плати не змінюється
>
> — `spec.md §5, AC-06, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] У задачі `control`: `encoders_state_get`, копія набору і ознаки збереження з `pid_hal` у вхід кроку; публікація кадру трасування раз на 5 тактів; рядки журналу на події відкату — `components/drive/drive_hal.c`
- [ ] `main.c`: `pid_hal_init()` після `storage_init()`, `pid_trace_start()`; колбеки `pid_*` у `console_ops_t` — `main/main.c`
- [ ] `components/drive/CMakeLists.txt`: PRIV_REQUIRES `pid` (HAL) і `encoders`
- [ ] Рядок регулятора в `status` (режим) — через наявний колбек статусу — `main/main.c`

## Edge cases

| Case | Behaviour |
|---|---|
| `control` стартує раніше за `storage_init` | до `pid_hal_init` набір «не налаштований», ровер як у фазі 4 |
| задача `pid_trace` не створилась | рядок журналу, решта працює |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень
- [ ] `test/host/run_tests.sh` зелений
- [ ] вільна внутрішня памʼять після старту ≥ 345000 байт — перевіряє Розробник у T10
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (статичні перевірки `test/host/check_*.sh`)
