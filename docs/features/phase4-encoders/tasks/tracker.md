# Tracker — phase4-encoders

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити encoders_core: приріст за модулем 30000, знак колеса, 64-бітний накопичувач, обнулення через зсув | domain | Агент | M | — | done | COMMIT |
| T2 | Додати в encoders_core швидкість колеса: приріст на фактичний час, нуль на порожньому інтервалі, лічильник інтервалів | domain | Агент | S | T1 | done | COMMIT |
| T3 | Додати в encoders_core одиниці (оберти, об/хв, см, см/с), доступність коліс і тексти знімка, enc reset та рядка status | domain | Агент | M | T2 | done | COMMIT |
| T4 | Додати в console_core команду enc: дві форми, хибні аргументи, дозвіл у всіх станах, рядок help | ports | Агент | S | — | done | COMMIT |
| T5 | Створити encoders_hal: 4 блоки лічильника з watch-point без callback, фільтр 1000 нс, задача 20 мс, стан під portMUX | infra | Агент | M | T3 | done | COMMIT |
| T6 | Підключити encoders_start і колбеки enc_snapshot, enc_reset, enc_status у main | wiring | Агент | S | T4, T5 | done | COMMIT |
| T7 | Додати check_calib.sh (константи енкодерів проти measured.md) і прогнати всі хостові запобіжники | tests | Агент | S | T6 | done | COMMIT |
| T8 | Написати acceptance.md: процедура приймання фази 4 на залізі для Розробника | docs | Розробник | S | T6 | done | COMMIT |
| T9 | Швидкість лише за інтервал не коротший за половину номіналу, з усім рухом від попередньої мітки (review F1) | domain | Агент | S | — | done | COMMIT |
| T10 | Причина недоступності — вказівник на статичний рядок замість копії, стан енкодерів < 200 байтів (review F5) | domain | Агент | S | T9 | done | COMMIT |
| T11 | Відмова enc називає, що саме не так: невідоме слово, помилка в reset, зайве слово (review F2) | ports | Агент | S | — | done | COMMIT |
| T12 | acceptance.md: крок AC-03 у FAILSAFE і факт часу хостового прогону (review F3, F4) | docs | Агент | S | — | done | COMMIT |

**Total:** 12 tasks, ~7.5 person-days (T9–T12 — виправлення за `_review/review-2026-10-19.md`).
