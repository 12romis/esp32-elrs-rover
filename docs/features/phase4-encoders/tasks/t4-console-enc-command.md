---
id: T4
title: "Додати в console_core команду enc: дві форми, хибні аргументи, дозвіл у всіх станах, рядок help"
layer: "ports"
deps: []
blocks: ["T6"]
acs: ["AC-07", "AC-09"]
files_hint: ["components/rover_console/console_core.c", "components/rover_console/include/console_core.h", "test/host/test_console.c", "components/rover_console/console_hal.c", "components/rover_console/include/console.h", "test/host/test_drive.c"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T4 — Додати в console_core команду enc: дві форми, хибні аргументи, дозвіл у всіх станах, рядок help

## Place in the sequence

- **Blocked by:** — · **Blocks:** T6 — Підключити encoders_start у main і колбеки enc_snapshot, enc_reset, enc_status у консоль · **Wave:** 1.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** щоб недоступні енкодери чи помилкова команда консолі лише пояснювались, а ровер далі озброювався й їздив
> **So that** вимірювання ніколи не стає причиною зупинки чи перезапуску в цій фазі
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Розбір і права нової команди, щоб помилкова форма лише пояснювалась і нічого не змінювала.

## Inlined context

> | `enc` | друкує знімок енкодерів |
> | `enc reset` | обнуляє лічильники й відстань доступних коліс і підтверджує |
> Будь-яка інша форма (невідоме друге слово, помилка в `reset`, третє слово) — клас «хибні аргументи»: відповідь називає, що саме не так, і перелічує обидві допустимі форми; нічого не змінюється.
> Команда дозволена в усіх 7 станах автомата й в обох режимах керування (стенд і пульт): моторами не керує. Відмови за станом немає.
> Додається рядок `enc [reset]` — знімок енкодерів і обнулення лічильників.
>
> — `contracts/cli.md, «Нова команда enc» і «help», abridged` · full text: [cli.md](../contracts/cli.md)

> Разом 10 команд × 7 станів × 2 режими = 140 пар.
>
> — `contracts/cli.md, «Права за станом», verbatim` · full text: [cli.md](../contracts/cli.md)

> **Hard rule:** Кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`. Логіка живе в core і покривається тестами на хості.
>
> — `constitution.md, п. 6, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Консольна команда `enc` / `enc reset` і поле `status` «енкодери» — див. витяг в Inlined context.

— `contracts/cli.md, «Нова команда enc», abridged` · full text: [cli.md](../contracts/cli.md)

## Acceptance criteria

### AC-07 (US-05) — error

> **Given** ровер у будь-якому стані
> **When** Розробник вводить команду енкодерів з невідомим аргументом, зайвими словами чи помилкою в слові обнулення
> **Then** команду відхилено з поясненням, які форми команди допустимі (знімок і обнулення); лічильники, швидкості, стан автомата й команди бортам не змінюються
>
> — `spec.md §5, AC-07, verbatim` · full text: [spec.md](../spec.md)

### AC-09 (US-02) — domain invariant

> **Given** ровер у будь-якому стані, зокрема в ARMED під час руху з пульта
> **When** Розробник обнуляє лічильники командою енкодерів
> **Then** лічильники й пройдена відстань усіх чотирьох коліс стають нулем одночасно, а консоль це підтверджує; швидкість не стрибає (вона рахується з різниці за інтервал), стан автомата, озброєння й команди бортам не змінюються; обнулити лічильники можна лише з консолі — пульт такої дії не має, а жодна консольна команда не змінює виміряні величини (імпульси на оберт, знак коліс, діаметр)
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

## Checklist

> Під час implement (2026-10-19): нові значення `console_cmd_t` ламають `switch` у `console_hal.c` (`-Werror=switch`), тож обробку `enc` у HAL консолі й три колбеки `console_ops_t` перенесено з T6 у T4 — задачі зв'язані компіляцією.

- [ ] Додати `CONSOLE_CMD_ENC` і, за потреби, `CONSOLE_CMD_ENC_RESET` у перелік команд — `console_core.h`
- [ ] Розпізнати перше слово `enc`, друге слово `reset` або порожньо; інше — `CONSOLE_CMD_BAD_ARGS` з текстом про допустимі форми — `console_core.c`
- [ ] Матриця дозволів: `enc` дозволена в усіх станах і режимах — `console_core.c`
- [ ] Рядок `help` — `console_core.c`
- [ ] Тести: обидві форми, `enc foo`, `enc Reset`, `enc reset x`, матриця 140 пар, рядок help — `test/host/test_console.c`

## Edge cases

| Case | Behaviour |
|---|---|
| `enc` з пробілами в кінці | як `enc` |
| `enc RESET` | хибні аргументи (регістр має значення) |
| `enc` у FAILSAFE чи BOOT | дозволено, без відмови за станом |

## Definition of Done

- [ ] `test/host/run_tests.sh` зелений з тестами розбору й матриці 140 пар; `idf.py build` без нових попереджень
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (для C: збірка без нових попереджень і хостові скрипти)
