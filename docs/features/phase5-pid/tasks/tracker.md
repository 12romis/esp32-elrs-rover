# Tracker — phase5-pid

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити pid_core: набір коефіцієнтів, перевірка чисел, запис із версією і CRC32 | domain | Агент | M | — | todo | — |
| T2 | Додати в pid_core крок регулятора: задана з мікшера, розгін, P/I/D, вихід у напрямку цілі, швидкість борту | domain | Агент | M | T1 | todo | — |
| T3 | Додати в pid_core кадр трасування і тексти: рядок трасування, показ стану регулятора | domain | Агент | S | T2 | todo | — |
| T4 | Вбудувати регулятор у drive_ctl_step: задана щотакту, крок лише на новому знімку, фіксація режиму при озброєнні | app | Агент | M | T2, T3 | todo | — |
| T5 | Додати в drive_ctl_step відкати і зупинки: недоступні енкодери, застарілий знімок, скидання при зупинках, відмова озброєння під час збереження | app | Агент | M | T4 | todo | — |
| T6 | Додати в console_core команду pid: форми, відмови, матриця дозволів за станом | ports | Агент | M | T1 | todo | — |
| T7 | Створити pid_hal: набір під portMUX, завантаження й збереження в NVS, задача pid_trace | infra | Агент | M | T1, T3 | todo | — |
| T8 | Підключити регулятор: drive_hal подає знімок і набір, main стартує pid_hal і pid_trace, колбеки консолі | wiring | Агент | M | T5, T6, T7 | todo | — |
| T9 | Посилити хостові перевірки: межа потужності 750, core без ESP-IDF, повний прогін з test_pid.c | tests | Агент | S | T8 | todo | — |
| T10 | Написати acceptance.md: процедура приймання фази 5 на залізі для Розробника | docs | Агент | S | T8 | todo | — |

**Total:** 10 tasks, ~8.5 person-days.
