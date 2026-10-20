---
id: T9
title: "Додати в ioexp_hal операцію зміни лінії скидання і внутрішній мʼютекс запису байта"
layer: "infra"
deps: ["T4"]
blocks: ["T10"]
acs: ["AC-08", "AC-16"]
files_hint: ["components/ioexp/ioexp_hal.c", "components/ioexp/include/ioexp_hal.h"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

# T9 — Додати в ioexp_hal операцію зміни лінії скидання і внутрішній мʼютекс запису байта

## Place in the sequence

- **Blocked by:** T4 — Розширити ioexp_core: біти ліній скидання далекомірів у тіньовому регістрі і збирання байта зі збереженням · **Blocks:** T10 — Створити tof_hal: драйвер VL53L0X (порт p4_tof.c), послідовність XSHUT з новими адресами, виняток у check_delays · **Wave:** 2.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** щоб вийнятий, непідключений чи завислий далекомір не зупиняв і не вішав прошивку, а я бачив це в консолі й міг повернути датчик у роботу в DISARMED
> **So that** одна відмова не виводить ровер з ладу, а сліпу зону я бачу і виправляю
>
> — `spec.md §4, US-03, verbatim` · full text: [spec.md](../spec.md)

Ця задача дає `sensors` безпечний спосіб піднімати й опускати лінії скидання датчиків, не заважаючи запису світла.

## Inlined context

> **Chosen:** варіант 1. Лише так розряди скидання пережили б перезапис раз на секунду без нового механізму: байт збирається в одному місці з копії світла й копії розрядів скидання, а підтвердження запису й повтор після збою лишаються логікою `ioexp`. Внутрішній мʼютекс `ioexp` береться на час запису байта;
>
> — `adr/0002-let-ioexp-own-the-whole-expander-byte-including-the-sensor-reset-lines.md, Decision outcome, abridged` · full text: [adr/0002-let-ioexp-own-the-whole-expander-byte-including-the-sensor-reset-lines.md](../adr/0002-let-ioexp-own-the-whole-expander-byte-including-the-sensor-reset-lines.md)

> - `ioexp`: у `ioexp_t` додається маска стану розрядів скидання; `ioexp_prepare` підставляє її в байт; нова операція `ioexp_hal_xshut(bit, high)` змінює розряд і пише байт; `ioexp_hal_apply` та новий виклик беруть внутрішній мʼютекс `ioexp`, бо тепер пишуть дві задачі (`ui` на core 0 і `sensors` на core 0). Перший запис 0x00 при старті лишається першою операцією на шині.
>
> — `sad.md §4, журнал припущень design, bullet «ioexp», verbatim` · full text: [sad.md](../sad.md)

> Усі три джерела запису — `ui`, щосекундний перезапис і повторна ініціалізація `sensors` — проходять через один мʼютекс `ioexp` і один тіньовий регістр, тож запис світла не опускає лінії скидання, а `tof reinit` не гасить і не запалює світло (AC-08, AC-16, ADR-0002).
>
> — `sad.md §6, «Flow 9: спільний байт розширювача — світло і лінії скидання», потік 9, verbatim` · full text: [sad.md](../sad.md)

> **Hard rule:** 7. Інваріанти безпеки, сильніші за будь-яку функціональність:    - STBY піднімається тільки в стані ARMED і тільки свідомо;    - втрата кадрів CRSF довша за 500 мс → ШІМ у нуль, STBY вниз;    - перша операція по I²C після старту — запис `0x00` у PCF8574;    - VBAT < 6.0 В → мотори не запускаються.
>
> — `constitution.md, п. 7, verbatim` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-08 (US-03) — cross-context

> **Given** ровер у DISARMED, один далекомір у відмові (AC-06 чи AC-07), шлейф підключено назад; фари увімкнено вручну, а задній ліхтар вимкнено
> **When** Розробник подає команду повторної ініціалізації далекомірів
> **Then** далекомір повертається в роботу: консоль підтверджує, показ називає його справним з відстанню, а фари не гаснуть і не блимають, ліхтар не змінюється; на час команди (до 3 с, ровер стоїть) опитування решти справних далекомірів призупиняється, а після її завершення вони знову дають відстані без хибних відмов; якщо далекомір досі не відповідає, консоль називає його відмовившим, а все інше лишається без змін
>
> — `spec.md §5, AC-08, verbatim` · full text: [spec.md](../spec.md)

### AC-16 (US-05) — cross-context

> **Given** ровер озброєний, фари й задні ліхтарі працюють, далекоміри опитуються
> **When** Розробник їде 60 с, кілька разів зупиняючись перед перешкодою, і робить кілька повторних ініціалізацій у DISARMED
> **Then** світло не блимає без причини, у журналі немає жодного попередження про помилку шини чи розширювача портів, а затримка «кадр пульта → борти» не перевищує межу §6; шина працює на 100 кГц
>
> — `spec.md §5, AC-16, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Створити внутрішній мʼютекс `ioexp` (чекання 50 мс) і брати його в `ioexp_hal_apply` та новій операції на час запису байта — `components/ioexp/ioexp_hal.c`
- [ ] Додати `ioexp_wr_t ioexp_hal_xshut(int bit, bool high)`: змінює бажаний розряд, пише байт, при невдалому записі `ioexp_xshut_revert` — `components/ioexp/ioexp_hal.c`, `components/ioexp/include/ioexp_hal.h`
- [ ] Перший запис 0x00 при старті і щосекундний перезапис лишаються (перезапис тепер з розрядами скидання) — `components/ioexp/ioexp_hal.c`
- [ ] Оновити коментар «не потокобезпечна» — `components/ioexp/include/ioexp_hal.h`

## Edge cases

| Case | Behaviour |
|---|---|
| `ui` і `sensors` пишуть одночасно | Записи йдуть по черзі; кожен байт містить обидві групи розрядів |
| Мʼютекс не взято за 50 мс | `IOEXP_WR_FAILED`, копія не змінюється, повтор автоматикою |
| Розширювач недоступний | `IOEXP_WR_UNAVAILABLE`, розряди не змінюються |
| Шина мертва під час підняття лінії | Бажаний розряд повернуто до підтвердженого, помилка повертається викликачеві |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень
- [ ] `check_delays.sh`, `check_pins.sh` чисті
- [ ] HAL — приймання на залізі T15 (AC-08, AC-16); хостового моку немає (пункт 6 конституції)
- [ ] every Hard Rule inlined above still holds
