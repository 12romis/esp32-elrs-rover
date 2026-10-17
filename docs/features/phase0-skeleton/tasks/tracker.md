# Tracker — phase0-skeleton

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити хостовий раннер і скрипти перевірок test/host | tests | Агент | M | — | done | COMMIT |
| T2 | Створити компонент app_state: таблиця переходів, запит переходу, історія (core) | domain | Агент | M | T1 | done | COMMIT |
| T3 | Створити console_core: збирання рядка, розбір команд, права за станом | app | Агент | M | T1, T2 | done | COMMIT |
| T4 | Створити storage_core і watchdog_core: політика сховища та політика годування | domain | Агент | S | T1 | done | COMMIT |
| T5 | Створити app_state_hal і storage_hal: мʼютекс, журнал переходів, ініціалізація NVS | infra | Агент | M | T2, T4 | done | COMMIT |
| T6 | Створити watchdog_hal: програмний panic для TWDT, задача heartbeat, причина скидання | infra | Агент | S | T4 | done | COMMIT |
| T7 | Створити console_hal: ввід-вивід USB, задача console, виконання команд | ports | Агент | M | T3, T5 | done | COMMIT |
| T8 | Скласти main.c: послідовність старту, REQUIRES, видалення main/app_states | wiring | Агент | S | T5, T6, T7 | done | COMMIT |
| T9 | Підготувати чек-лист приймання фази 0 на залізі для журналу bring-up | docs | Агент | S | T8 | done | COMMIT |

**Total:** 9 tasks, ~7.0 person-days.
