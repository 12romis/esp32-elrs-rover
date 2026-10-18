---
id: T3
title: "Додати drive_ctl: запити arm/disarm/drive, дозволи, причини відмов"
layer: "app"
deps: ["T1", "T2"]
blocks: ["T4"]
acs: ["AC-01", "AC-02", "AC-09", "AC-13"]
files_hint: ["components/drive/", "test/host/test_drive.c"]
owner: "Агент"
estimate: "M"
context_budget: "M"
status: "done"
step: "GATE (без коміту — за вказівкою)"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T3 — Додати drive_ctl: запити arm/disarm/drive, дозволи, причини відмов

## Place in the sequence

- **Blocked by:** T1 — Створити battery_core: відліки в мВ, поріг 6000 мВ, свіжість знімка, T2 — Створити drive_core: мікшер, вирівнювання, обмеження 0.75, рівні виходів · **Blocks:** T4 — Додати drive_ctl: вікно команди 2 с, відсічка по напрузі, порядок зупинки · **Wave:** 2.
- **Lane:** lane drive_core (T2 → T3 → T4 — послідовно).

## Why (user story)

> **As a** Розробник  
> **I want** окремими командами свідомо озброїти ровер і в будь-який момент роззброїти його  
> **So that** шлях «зупинити» завжди під рукою, а рух можливий лише після мого явного дозволу
>
> — `spec.md §4, US-01, verbatim` · full text: [spec.md](../spec.md)

Дає чисту функцію кроку `drive_ctl_step`, яка за станом автомата, знімком напруги й запитом повертає рішення: прийняти чи відхилити `arm`, `disarm`, `drive`, з причиною відмови, а також виходи.

## Inlined context

> Задача `control` (`vTaskDelayUntil`, 20 мс, core 1, пріоритет 5) єдина пише в лінії моторів і єдина запитує переходи ARMED, DISARMED та FAILSAFE у фазі 1; консоль надсилає запити через чергу довжини 4 і чекає відповіді до 200 мс; запит старший за 150 мс `control` відкидає. Кожен такт задача перевіряє стан автомата й примусово вимикає виходи, якщо стан не ARMED.
>
> — `adr/0001, Decision outcome, abridged` · full text: [adr/0001](../adr/0001-run-motor-safety-logic-in-a-dedicated-control-task.md)

> Зупинка — це послідовність у `drive_core`: нульовий ШІМ, DIR 0/0, STBY низька, і лише після цього `control` запитує перехід у новий стан. Ті самі рівні тримаються в кожному такті, коли стан не ARMED.
>
> — `adr/0002, Decision outcome, abridged` · full text: [adr/0002](../adr/0002-stop-by-coasting-with-outputs-off-before-state-write.md)

> Озброїти: control читає знімок напруги. Якщо напруга валідна й не нижча за поріг — запит переходу в ARMED і «прийнято». Інакше — відхилено з причиною (напруга й поріг, або «вимірювання ще немає»). disarm: ШІМ 0, DIR 0/0, STBY вниз; якщо автомат у ARMED — запит DISARMED; у решті станів стан не змінюється.
>
> — `sad.md §6, runtime view, abridged` · full text: [sad.md](../sad.md)

> | Команда | BOOT | SELFTEST | DISARMED | ARMED | OBSTACLE_HOLD | FAILSAFE | FAULT |
> |---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
> | help, status | так | так | так | так | так | так | так |
> | arm | ні | ні | так (за напруги не нижче порога) | ні | ні | ні | ні |
> | drive | ні | ні | ні | так | ні | ні | ні |
> | disarm | вимикає мотори | вимикає мотори | вимикає мотори | → DISARMED | вимикає мотори | вимикає мотори | вимикає мотори |
> | reboot | так | ні | так | ні | ні | ні | ні |
> | wdt-test | ні | ні | так | ні | ні | ні | ні |
>
> — `spec.md §5, «Матриця дозволів команд за станом», verbatim` · full text: [spec.md](../spec.md)

> STBY піднімається тільки в стані ARMED і тільки свідомо; ШІМ обмежений 0.75 до завершення фази 5; VBAT < 6.0 В → мотори не запускаються.
>
> — `constitution.md, п. 7, abridged` · full text: [constitution.md](../../../constitution.md)

> Файли `*_core.c` не підключають жодного заголовка ESP-IDF чи FreeRTOS; вся логіка тут, її тести — на звичайному gcc у `test/host/`; для HAL моки не пишуться, критерій приймання — перевірка на залізі, записана в журнал bring-up.
>
> — `constitution.md, п. 6, abridged` · full text: [constitution.md](../../../constitution.md)

> Будь-яка задача вважається завершеною лише якщо `idf.py build` проходить без помилок і без нових попереджень. Мова коментарів і комітів — українська; ідентифікатори — англійська.
>
> — `constitution.md, п. 8–9, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** недостатньо або суперечить коду → прочитати названий файл повністю ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) і діяти за ним. Не вигадувати.

## Data delta

No DB changes.

## API contract

Internal — no API surface. Внутрішній інтерфейс: `drive_ctl_step(ctl, in, out)` у `drive_core.h`; `in` = {now_ms, стан автомата, знімок напруги, запит {NONE, ARM, DISARM, DRIVE(тяга, поворот)}}; `out` = {виходи, запит стану {NONE, ARMED, DISARMED, FAILSAFE}, відповідь {прийнято, причина, застосовані значення бортів}}.

## Acceptance criteria

### AC-01 (US-01) — happy path

> **Given** автомат у стані DISARMED, напруга акумулятора не нижча за поріг, консоль активна
**When** Розробник вводить команду озброєння
**Then** консоль підтверджує, що ровер озброєно, записує перехід у ARMED, а колеса лишаються нерухомими, доки не надійде команда руху
>
> — `spec.md §5, AC-01, verbatim` · full text: [spec.md](../spec.md)

### AC-02 (US-01) — happy path

> **Given** ровер озброєно і борти крутяться за командою руху
**When** Розробник вводить команду роззброєння
**Then** обидва борти зупиняються одразу, лінія STBY опускається, автомат переходить у DISARMED, а консоль підтверджує зупинку
>
> — `spec.md §5, AC-02, verbatim` · full text: [spec.md](../spec.md)

### AC-09 (US-01) — domain invariant

> **Given** автомат у будь-якому стані, зокрема в DISARMED, BOOT, SELFTEST чи FAILSAFE
**When** Розробник вводить команду роззброєння
**Then** консоль підтверджує, що моторів вимкнено (потужність нуль, STBY низька); автомат переходить у DISARMED лише там, де таблиця переходів це дозволяє (з ARMED), а в решті станів лишається в своєму стані й консоль пояснює, чому стан не змінився
>
> — `spec.md §5, AC-09, verbatim` · full text: [spec.md](../spec.md)

### AC-13 (US-05) — domain invariant

> **Given** автомат у DISARMED, а виміряна напруга акумулятора нижча за 6.0 В, або батареї немає (плата живиться лише від USB), або першого вимірювання напруги після старту ще немає
**When** Розробник вводить команду озброєння
**Then** озброєння відхилено з поясненням, яка напруга виміряна і який поріг (або що вимірювання ще немає), автомат лишається в DISARMED, а STBY низька
>
> — `spec.md §5, AC-13, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `drive_core.h`: `drive_req_t`, `drive_reason_t {NONE, NOT_DISARMED, NOT_ARMED, LOW_VOLTAGE, NO_MEASUREMENT}`, `drive_ctl_t`, `drive_ctl_in_t`, `drive_ctl_out_t`
- [ ] `drive_core.c`: `drive_ctl_init`, `drive_ctl_step`: arm лише в DISARMED за придатного знімка не нижче порога; drive лише в ARMED (приймає вхід, викликає мікшер із T2); disarm у будь-якому стані повертає вимкнені виходи, а запит DISARMED — лише з ARMED
- [ ] `test/host/test_drive.c`: arm у DISARMED при 6000 мВ, 5999 мВ, без знімка, зі знімком 601 мс; arm у решті шести станів; drive у ARMED і поза ARMED; disarm у всіх семи станах (виходи вимкнено, запит DISARMED лише з ARMED)
- [ ] Усі нові вхідні значення тяги й повороту поза [−1, +1] ядро відхиляє як неприйняті (розбір рядка — T5)

## Edge cases

| Case | Behaviour |
|---|---|
| arm без першого вимірювання напруги | відхилено, причина NO_MEASUREMENT, стан без змін |
| arm при напрузі нижче порога | відхилено, причина LOW_VOLTAGE із виміряною напругою |
| disarm у FAILSAFE, BOOT, SELFTEST, OBSTACLE_HOLD, FAULT, DISARMED | виходи вимкнено, стан не змінюється |
| disarm у ARMED | виходи вимкнено раніше, ніж запитано DISARMED |
| drive не в ARMED | відхилено, причина NOT_ARMED, виходи вимкнені |

## Definition of Done

- [ ] Хостові тести дозволів і причин проходять (arm 7 станів, drive 7 станів, disarm 7 станів)
- [ ] У жодному кроці запит стану не з'являється раніше за вимкнені виходи
- [ ] `check_core_headers.sh` чистий
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
