---
id: T9
title: "Скласти main.c: порядок старту фази 1 і REQUIRES"
layer: "wiring"
deps: ["T7", "T8"]
blocks: ["T10"]
acs: ["AC-15"]
files_hint: ["main/"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "review"
step: "BUILD (очікує приймання Розробника на залізі)"
---

<!-- Самодостатня задача: працюй із вкладеного; джерело завжди перемагає (див. підпис кожного уривка). -->

# T9 — Скласти main.c: порядок старту фази 1 і REQUIRES

## Place in the sequence

- **Blocked by:** T7 — Створити drive_hal: LEDC і GPIO моторів, задача control, черга запитів, Task WDT, T8 — Підключити arm, disarm, drive і розширений status до console_hal · **Blocks:** T10 — Підготувати чек-лист приймання фази 1 на залізі і фінальний прогін перевірок · **Wave:** 6.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник  
> **I want** щоб при напрузі нижче 6.0 В мотори не стартували, а в русі зупинялися  
> **So that** розряджений акумулятор не пошкоджується і не дає хибних результатів приймання
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Збирає старт: виходи моторів вимкнено першими, потім сторожовий таймер і консоль, вимірювання напруги, задача керування, сховище і лише тоді SELFTEST та DISARMED.

## Inlined context

> Задача `control` (`vTaskDelayUntil`, 20 мс, core 1, пріоритет 5) єдина пише в лінії моторів і єдина запитує переходи ARMED, DISARMED та FAILSAFE у фазі 1; консоль надсилає запити через чергу довжини 4 і чекає відповіді до 200 мс; запит старший за 150 мс `control` відкидає. Кожен такт задача перевіряє стан автомата й примусово вимикає виходи, якщо стан не ARMED.
>
> — `adr/0001, Decision outcome, abridged` · full text: [adr/0001](../adr/0001-run-motor-safety-logic-in-a-dedicated-control-task.md)

> `app_main`: `app_state_hal_init` → `drive_hal_init` (виходи вимкнено) → `watchdog_start` → `console_start` → `battery_start` → `drive_start` (control) → `storage_init` → SELFTEST → DISARMED. `control` стартує до `storage_init`, тож `disarm` працює і в BOOT, якщо сховище не піднялося. SELFTEST і перший DISARMED на старті запитує `main`, як у фазі 0.
>
> — `sad.md §5 і §4, abridged` · full text: [sad.md](../sad.md)

> Старт перевіряється: ровер доходить до DISARMED, консоль друкує попередження про напругу нижче порога, моторів не запущено; прошивка переводить лінії моторів у вимкнений стан не пізніше завершення стану BOOT. Захищені файли (`CMakeLists.txt` кореня, `.vscode/`, `sdkconfig.defaults`) без згоди Розробника не змінюються.
>
> — `spec.md §5 AC-15 і constitution.md п. 10, abridged` · full text: [spec.md](../spec.md)

> Будь-яка задача вважається завершеною лише якщо `idf.py build` проходить без помилок і без нових попереджень. Мова коментарів і комітів — українська; ідентифікатори — англійська.
>
> — `constitution.md, п. 8–9, abridged` · full text: [constitution.md](../../../constitution.md)

**Fallback:** недостатньо або суперечить коду → прочитати названий файл повністю ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) і діяти за ним. Не вигадувати.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-15 (US-05) — domain invariant

> **Given** плата стартує з акумулятором, напруга якого нижча за 6.0 В, або без акумулятора
**When** автомат проходить старт
**Then** ровер доходить до DISARMED, як в AC-01 фази 0, консоль друкує попередження про напругу нижче порога, моторів не запущено; до ініціалізації ліній моторів STBY тримає апаратна притяжка, а прошивка переводить лінії моторів у вимкнений стан (STBY низька, потужність нуль, обидві лінії напрямку низькі) не пізніше завершення стану BOOT
>
> — `spec.md §5, AC-15, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `main/main.c`: порядок старту за sad.md §5; помилка `drive_hal_init` не зупиняє консоль (стан лишається BOOT, моторів не запущено)
- [ ] `s_console_ops`: заповнити нові колбеки викликами `drive_hal_submit`, `drive_hal_status`, `battery_get_snapshot`
- [ ] `main/CMakeLists.txt`: додати `drive`, `battery` до `REQUIRES` (не `CMakeLists.txt` кореня)
- [ ] `idf.py build` без помилок і нових попереджень; `check_protected.sh` чистий

## Edge cases

| Case | Behaviour |
|---|---|
| Сховище не ініціалізувалось | автомат у BOOT, консоль і control працюють, disarm виводить, що моторів не запущено |
| Збій drive_hal_init | моторів не запущено, консоль повідомляє, старт не доходить до ARMED |
| Батареї немає | DISARMED, попередження в журналі, arm відхиляється |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень
- [ ] `check_protected.sh`, `check_pins.sh`, `check_core_headers.sh` чисті
- [ ] Старт на залізі проходить до DISARMED (записує Розробник у журнал bring-up)
- [ ] `idf.py build` без помилок і без нових попереджень; Hard rules вище виконано
