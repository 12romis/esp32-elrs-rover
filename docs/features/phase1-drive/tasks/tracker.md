# Tracker — phase1-drive

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити battery_core: відліки в мВ, поріг 6000 мВ, свіжість знімка | domain | Агент | M | — | done | GATE (без коміту — за вказівкою) |
| T2 | Створити drive_core: мікшер, вирівнювання, обмеження 0.75, рівні виходів | domain | Агент | M | — | done | GATE (без коміту — за вказівкою) |
| T3 | Додати drive_ctl: запити arm/disarm/drive, дозволи, причини відмов | app | Агент | M | T1, T2 | done | GATE (без коміту — за вказівкою) |
| T4 | Додати drive_ctl: вікно команди 2 с, відсічка по напрузі, порядок зупинки | app | Агент | M | T3 | done | GATE (без коміту — за вказівкою) |
| T5 | Розширити console_core: arm, disarm, drive, суворий розбір чисел, матриця дозволів | app | Агент | M | — | done | GATE (без коміту — за вказівкою) |
| T6 | Створити battery_hal: АЦП з калібруванням, задача battery, знімок напруги | infra | Агент | M | T1 | review | BUILD (очікує приймання Розробника на залізі) |
| T7 | Створити drive_hal: LEDC і GPIO моторів, задача control, черга запитів, Task WDT | infra | Агент | M | T4, T6 | review | BUILD (очікує приймання Розробника на залізі) |
| T8 | Підключити arm, disarm, drive і розширений status до console_hal | ports | Агент | S | T5, T7 | done | ПРИЙНЯТО на залізі (без коміту — за вказівкою) |
| T9 | Скласти main.c: порядок старту фази 1 і REQUIRES | wiring | Агент | S | T7, T8 | review | BUILD (очікує приймання Розробника на залізі) |
| T10 | Підготувати чек-лист приймання фази 1 на залізі і фінальний прогін перевірок | docs | Агент | S | T9 | done | GATE (без коміту — за вказівкою) |

**Total:** 10 tasks, ~8.5 person-days.
