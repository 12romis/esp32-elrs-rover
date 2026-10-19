# Tracker — phase4-encoders

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити encoders_core: приріст за модулем 30000, знак колеса, 64-бітний накопичувач, обнулення через зсув | domain | Агент | M | — | todo | — |
| T2 | Додати в encoders_core швидкість колеса: приріст на фактичний час, нуль на порожньому інтервалі, лічильник інтервалів | domain | Агент | S | T1 | todo | — |
| T3 | Додати в encoders_core одиниці (оберти, об/хв, см, см/с), доступність коліс і тексти знімка, enc reset та рядка status | domain | Агент | M | T2 | todo | — |
| T4 | Додати в console_core команду enc: дві форми, хибні аргументи, дозвіл у всіх станах, рядок help | ports | Агент | S | — | todo | — |
| T5 | Створити encoders_hal: 4 блоки лічильника з watch-point без callback, фільтр 1000 нс, задача 20 мс, стан під portMUX | infra | Агент | M | T3 | todo | — |
| T6 | Підключити encoders_start у main і колбеки enc_snapshot, enc_reset, enc_status у консоль | wiring | Агент | S | T4, T5 | todo | — |
| T7 | Додати check_calib.sh (константи енкодерів проти measured.md) і прогнати всі хостові запобіжники | tests | Агент | S | T6 | todo | — |
| T8 | Написати acceptance.md: процедура приймання фази 4 на залізі для Розробника | docs | Розробник | S | T6 | todo | — |

**Total:** 8 tasks, ~5.5 person-days.
