---
id: T6
title: "Додати в console_core команду pid: форми, відмови, матриця дозволів за станом"
layer: "ports"
deps: ["T1"]
blocks: ["T8"]
acs: ["AC-04", "AC-09", "AC-15"]
files_hint: ["components/rover_console/console_core.c", "components/rover_console/include/console_core.h", "test/host/test_console.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "M"
context_budget: "M"
status: "todo"
step: ""
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T6 — Додати в console_core команду pid: форми, відмови, матриця дозволів за станом

## Place in the sequence

- **Blocked by:** T1 — Створити pid_core: набір коефіцієнтів, перевірка чисел, запис із версією і CRC32 · **Blocks:** T8 — Підключити регулятор: drive_hal подає знімок і набір, main стартує pid_hal і pid_trace, колбеки консолі · **Wave:** 2 — після перевірки чисел з T1; паралельно з T2–T5.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** змінювати коефіцієнти регулятора командами консолі на ходу, вмикати й вимикати регулятор і бачити трасування цілі, виміру й виходу кожного борту
> **So that** я підбираю набір коефіцієнтів на залізі без перепрошивки
>
> — `spec.md §4, US-03, verbatim` · full text: [spec.md](../spec.md)

Дає Розробнику команди налаштування і не пускає небезпечні: перемикання режиму й збереження під час руху, зміну межі потужності.

## Inlined context

> Консоль: `CONSOLE_CMD_PID` у `console_core`; форми `pid`, `pid kp|ki|kd|ramp <число>`, `pid on`, `pid off`, `pid save`, `pid trace on`, `pid trace off`; `pid limit …` і будь-яка спроба змінити межу — окрема відмова (spec AC-09); решта — відмова з переліком форм (spec AC-04). Матриця дозволів (spec §5) — у `console_core` за станом автомата. Нові колбеки `console_ops_t`: `pid_show`, `pid_set_param`, `pid_set_mode`, `pid_save`, `pid_trace`.
>
> — `sad.md §4, журнал припущень «Консоль», verbatim` · full text: [sad.md](../sad.md)

> | Команда | BOOT | SELFTEST | DISARMED | ARMED | OBSTACLE_HOLD | FAILSAFE | FAULT |
> |---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
> | pid (показ стану) | так | так | так | так | так | так | так |
> | pid kp, ki, kd, ramp (зміна числа) | так | так | так | так | так | так | так |
> | pid on, pid off (режим) | ні | ні | так | ні | ні | ні | ні |
> | pid save (збереження) | так | так | так | ні | ні | так | так |
> | pid trace on, pid trace off | так | так | так | так | так | так | так |
>
> — `spec.md §5, «Матриця дозволів консольних команд за станом», verbatim` · full text: [spec.md](../spec.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface (консоль описано в spec §5).

## Acceptance criteria

### AC-04 (US-03) — error

> **Given** ровер у будь-якому стані автомата
> **When** Розробник вводить команду регулятора з відʼємним, нечисловим чи нескінченним значенням, з комою замість крапки, з невідомою назвою коефіцієнта, з нульовим темпом розгону, без значення, із зайвими словами чи зі спробою змінити межу потужності
> **Then** команду відхилено з поясненням, які форми команди допустимі (показ, зміна одного з чотирьох чисел, увімкнення, вимкнення, збереження, трасування) і що коефіцієнти мають бути скінченними й невідʼємними, а темп розгону — додатним; спроба змінити межу потужності відхиляється з окремим поясненням за AC-09; набір, режим регулятора, стан автомата і команди бортам не змінюються
>
> — `spec.md §5, AC-04, verbatim` · full text: [spec.md](../spec.md)

### AC-09 (US-05) — domain invariant

> **Given** регулятор увімкнено з будь-яким допустимим набором, зокрема свідомо завеликим Kp
> **When** Розробник тримає повний стік у будь-якому напрямку, притискає колесо до зупинки чи вводить команду, що намагається змінити межу потужності
> **Then** вихід регулятора кожного борту в жодному такті не перевищує межу потужності (0.75 у фазі 5), і трасування ніде не показує вихід понад межу; спроба змінити межу з консолі відхиляється з поясненням, що межа потужності — виміряна величина, яку змінює лише правка прошивки після запису в журнал вимірювань; пульт і консоль не мають жодного способу обійти межу
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

### AC-15 (US-03) — authorization

> **Given** ровер у ARMED (з пульта або в стендовому режимі)
> **When** Розробник намагається ввімкнути чи вимкнути регулятор або зберегти набір
> **Then** перемикання режиму відхилено з поясненням, що вмикати й вимикати регулятор можна лише в DISARMED, а збереження — з поясненням, що запис у памʼять плати під час руху заборонений (лише поза ARMED і OBSTACLE_HOLD); режим, набір у памʼяті плати, стан автомата і команди бортам не змінюються; зміна чисел набору, показ стану і трасування в ARMED дозволені (матриця нижче); пульт не має жодної дії над набором чи режимом регулятора; озброєння, що збіглося із записом набору в памʼять плати, відхиляється з поясненням «триває збереження набору»
>
> — `spec.md §5, AC-15, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `CONSOLE_CMD_PID` і розбір форм: `pid`, `pid kp|ki|kd|ramp <число>`, `pid on`, `pid off`, `pid save`, `pid trace on`, `pid trace off`; перевірка числа — функція з T1 — `components/rover_console/console_core.c`
- [ ] Окрема відмова на будь-яку спробу змінити межу (`pid limit …`) з поясненням AC-09; решта — відмова з переліком форм — `components/rover_console/console_core.c`
- [ ] Рядки матриці дозволів за станом автомата — `components/rover_console/console_core.c`
- [ ] Нові колбеки `console_ops_t`: `pid_show`, `pid_set_param`, `pid_set_mode`, `pid_save`, `pid_trace` — `components/rover_console/include/console_core.h`
- [ ] Хостові тести: усі форми й відмови AC-04, матриця 16 команд × 7 станів — `test/host/test_console.c`

## Edge cases

| Case | Behaviour |
|---|---|
| `pid kp 1,2` | відмова: кома замість крапки |
| `pid foo 1` | відмова з переліком форм |
| `pid limit 1.0` | окрема відмова: межа виміряна, змінюється лише правкою прошивки |
| `pid on` у ARMED | відмова: лише в DISARMED |
| `pid save` у ARMED чи OBSTACLE_HOLD | відмова: запис під час руху заборонений |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений з тестами форм і матриці
- [ ] `idf.py build` без помилок і нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (статичні перевірки `test/host/check_*.sh`)
