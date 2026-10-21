# Tracker — phase7-telemetry

> Status of every task in the epic. `implement` updates **Status** and **Step** after every subagent it runs.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити telemetry_core: кадр датчика батареї, рішення «слати чи ні», детектор зміни збою, статистика часу кроку й рядок status | domain | Агент | M | — | done | GATE |
| T2 | Навчити ui_core подвійному біпу на вході в OBSTACLE_HOLD і на виході в ARMED, жовтому індикатору й пріоритетам сигналів | domain | Агент | M | — | done | GATE |
| T3 | Навчити ui_core блиманню фар при зайнятому передньому секторі й задніх ліхтарів при зайнятому задньому в OBSTACLE_HOLD | domain | Агент | M | T2 | done | GATE |
| T4 | Додати в drive_status_t прапорці секторів і дві відмітки часу навколо кроку control з записом у telemetry_step_record | infra | Агент | S | T1 | review | REVIEW |
| T5 | Додати crsf_hal_send: запис готового кадру в UART2 власником лінії без блокування керування | infra | Агент | S | — | review | REVIEW |
| T6 | Створити telemetry_hal: задача telemetry (200 мс) читає знімок напруги й передає кадр через crsf_hal_send | infra | Агент | S | T1, T5 | review | REVIEW |
| T7 | Підключити в ui_hal прапорці секторів, жовтий колір індикатора й колір у ui_hal_status_t | infra | Агент | S | T2, T3, T4 | review | REVIEW |
| T8 | Підключити в main старт telemetry і колбек step_status, показати колір індикатора й час кроку в status | wiring | Агент | M | T1, T4, T6, T7 | done | GATE |
| T9 | Написати README (опис, збірка, запуск, інтерфейси) і блок-схему в docs/architecture.md | docs | Агент | M | T8 | done | GATE |
| T10 | Скласти чекліст самоперевірки docs/self-check.md за таблицею документа 02 §5 із посиланнями на докази | docs | Агент | M | T9 | done | GATE |
| T11 | Прогнати повну збірку, хостові тести й статичні перевірки, виправити порушення | tests | Агент | S | T8, T10 | done | GATE |
| T12 | Написати acceptance.md: процедура приймання фази 7 на залізі для Розробника | docs | Агент | S | T11 | done | GATE |

**Total:** 12 tasks, ~15 person-days.
