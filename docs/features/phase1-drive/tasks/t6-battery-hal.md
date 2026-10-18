---
id: T6
title: "Створити battery_hal: АЦП з калібруванням, задача battery, знімок напруги"
layer: "infra"
deps: ["T1"]
blocks: ["T7"]
acs: ["AC-13", "AC-14", "AC-15"]
files_hint: ["components/battery/"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "review"
step: "BUILD (очікує приймання Розробника на залізі)"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T6 — Створити battery_hal: АЦП з калібруванням, задача battery, знімок напруги

## Place in the sequence

- **Blocked by:** T1 — Створити battery_core: відліки в мВ, поріг 6000 мВ, свіжість знімка · **Blocks:** T7 — Створити drive_hal: LEDC і GPIO моторів, задача control, черга запитів, Task WDT · **Wave:** 2.
- **Lane:** lane battery (T1 → T6 — послідовно).

## Why (user story)

> **As a** Розробник  
> **I want** щоб при напрузі нижче 6.0 В мотори не стартували, а в русі зупинялися  
> **So that** розряджений акумулятор не пошкоджується і не дає хибних результатів приймання
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Дає апаратну частину вимірювання: `adc_oneshot` + curve fitting на GPIO1, задачу `battery` (200 мс), публікацію знімка і попередження в журнал після першого вимірювання, якщо напруга нижча за поріг або батареї немає.

## Inlined context

> Окрема задача `battery` (200 мс) публікує знімок «мВ, час, валідність»; `battery_core` рахує мВ і порівнює з порогом 6000 мВ. Знімок старший за 600 мс (три періоди) або невалідний: `arm` відхиляється, а в ARMED `control` зупиняє мотори й просить FAILSAFE.
>
> — `adr/0003, Decision outcome, abridged` · full text: [adr/0003](../adr/0003-measure-battery-in-own-task-with-stale-reading-failsafe.md)

> Задача `battery`: core 0, пріоритет 2, 200 мс через `vTaskDelayUntil`; на кожен такт — пакет з 16 відліків АЦП, усереднення в core. Послаблення `ADC_ATTEN_DB_12`; піни лише з `board_pins.h` (`PIN_VBAT_SENSE`).
>
> — `sad.md §4, журнал припущень, і docs/02-FIRMWARE-CONTEXT.md §3.1, abridged` · full text: [sad.md](../sad.md)

> Номери GPIO існують рівно в одному файлі: `components/board/include/board_pins.h`. Літерал номера піна будь-де ще — дефект.
>
> — `constitution.md, п. 2, abridged` · full text: [constitution.md](../../../constitution.md)

> Усі періодичні задачі використовують `vTaskDelayUntil`, а не `vTaskDelay`. Мотори вмикаються лише після прозвонювання шлейфів (виконано 2026-10-17).
>
> — `constitution.md, п. 12 і 14, abridged` · full text: [constitution.md](../../../constitution.md)

> Будь-яка задача вважається завершеною лише якщо `idf.py build` проходить без помилок і без нових попереджень. Мова коментарів і комітів — українська; ідентифікатори — англійська.
>
> — `constitution.md, п. 8–9, abridged` · full text: [constitution.md](../../../constitution.md)

> Для HAL тести на моках не пишуться: критерій приймання — перевірка на залізі, записана в журнал bring-up. AC-13…AC-15 на залізі не приймаються, доки не закрито spec OQ-1 (друга точка калібрування).
>
> — `constitution.md п. 6 і spec.md §8 OQ-1, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** недостатньо або суперечить коду → прочитати названий файл повністю ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) і діяти за ним. Не вигадувати.

## Data delta

No DB changes.

## API contract

Internal — no API surface. Публічне API: `battery_start()`, `battery_get_snapshot(*snap)` (копія під критичним блоком).

## Acceptance criteria

### AC-13 (US-05) — domain invariant

> **Given** автомат у DISARMED, а виміряна напруга акумулятора нижча за 6.0 В, або батареї немає (плата живиться лише від USB), або першого вимірювання напруги після старту ще немає
**When** Розробник вводить команду озброєння
**Then** озброєння відхилено з поясненням, яка напруга виміряна і який поріг (або що вимірювання ще немає), автомат лишається в DISARMED, а STBY низька
>
> — `spec.md §5, AC-13, verbatim` · full text: [spec.md](../spec.md)

### AC-14 (US-05) — cross-context

> **Given** ровер озброєно (їде за командою руху чи ще ні), а напруга акумулятора падає нижче 6.0 В
**When** вимірювання напруги фіксує значення нижче порога, і це відбувається не пізніше 1 с після того, як напруга опустилася під поріг
**Then** борти зупиняються (якщо їхали), STBY опускається, автомат переходить у FAILSAFE, консоль називає причину й виміряну напругу, а подальше озброєння відхиляється з поясненням, що вихід з FAILSAFE можливий лише апаратним скиданням плати або перемиканням живлення (команда перезавантаження в цьому стані не діє)
>
> — `spec.md §5, AC-14, verbatim` · full text: [spec.md](../spec.md)

### AC-15 (US-05) — domain invariant

> **Given** плата стартує з акумулятором, напруга якого нижча за 6.0 В, або без акумулятора
**When** автомат проходить старт
**Then** ровер доходить до DISARMED, як в AC-01 фази 0, консоль друкує попередження про напругу нижче порога, моторів не запущено; до ініціалізації ліній моторів STBY тримає апаратна притяжка, а прошивка переводить лінії моторів у вимкнений стан (STBY низька, потужність нуль, обидві лінії напрямку низькі) не пізніше завершення стану BOOT
>
> — `spec.md §5, AC-15, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `battery_hal.h` і `battery_hal.c`: `adc_oneshot_new_unit` ADC1, канал для `PIN_VBAT_SENSE`, `adc_cali_create_scheme_curve_fitting`, `ADC_ATTEN_DB_12`
- [ ] задача `battery` (core 0, пріоритет 2, `vTaskDelayUntil` 200 мс): 16 відліків → `battery_avg_mv` → `battery_adc_to_mv` → знімок {мВ, час, валідність}
- [ ] після першого вимірювання `ESP_LOGW`, якщо напруга нижча за поріг (також без батареї)
- [ ] `CMakeLists.txt`: додати `battery_hal.c`, `PRIV_REQUIRES esp_adc esp_timer freertos log`; `idf.py build` без нових попереджень

## Edge cases

| Case | Behaviour |
|---|---|
| Живлення лише від USB, батареї немає | знімок валідний із низькою напругою; попередження в журнал |
| Задача battery зупинилась | знімок старіє; drive_ctl вважає його непридатним через 600 мс |
| Збій ініціалізації АЦП | знімок лишається невалідним, arm відхиляється |
| Друкована похибка АЦП | значення приймаються попередніми до закриття OQ-1 |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень
- [ ] Чек-лист приймання на залізі записано (T10), запис у журнал bring-up виконує Розробник
- [ ] Піни лише з `board_pins.h` (`check_pins.sh` чистий)
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
