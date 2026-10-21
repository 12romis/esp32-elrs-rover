# Tracker — phase6-tof

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити tof_core: медіана, сектори, зайнятість, звільнення, відмова датчика і калібрувальну константу порогу | domain | Агент | M | — | done | COMMIT |
| T2 | Додати в tof_core обмеження тяги пари бортів і тексти: показ датчиків і рядок входу в утримання | domain | Агент | M | T1 | done | COMMIT |
| T3 | Додати в app_state предикат «ровер їде» і пару OBSTACLE_HOLD → DISARMED (12 пар із 49) | domain | Агент | S | — | done | COMMIT |
| T4 | Розширити ioexp_core: біти ліній скидання далекомірів у тіньовому регістрі і збирання байта зі збереженням | domain | Агент | S | — | done | COMMIT |
| T5 | Вбудувати в drive_ctl_step знімок далекомірів: обмеження тяги після мікшера, запити ARMED ↔ OBSTACLE_HOLD, застарілий знімок | app | Агент | M | T2, T3 | done | COMMIT |
| T6 | Перевести в drive_core усі перевірки «лише ARMED» на предикат: відсічка, втрата зв'язку, пульт, роззброєння, вікно команди, регулятор | app | Агент | M | T5 | done | COMMIT |
| T7 | Навчити ui_core OBSTACLE_HOLD: задні ліхтарі авто, вхід у рух вмикає фари й сигнал, перехід між ARMED і OBSTACLE_HOLD їх не чіпає | app | Агент | S | T3 | done | COMMIT |
| T8 | Додати в console_core команду tof (показ і reinit), матрицю дозволів і прийом drive в OBSTACLE_HOLD | ports | Агент | M | T3 | done | COMMIT |
| T9 | Додати в ioexp_hal операцію зміни лінії скидання і внутрішній мʼютекс запису байта | infra | Агент | S | T4 | done | COMMIT |
| T10 | Створити tof_hal: драйвер VL53L0X (порт p4_tof.c), послідовність XSHUT з новими адресами, виняток у check_delays | infra | Агент | M | T1, T9 | done | COMMIT |
| T11 | Додати в tof_hal задачу sensors: опитування раз на 50 мс, знімок під portMUX, відмова за 250 мс, tof reinit у DISARMED | infra | Агент | M | T10 | done | COMMIT |
| T12 | Підключити далекоміри до drive_hal: знімок у кроці, запит OBSTACLE_HOLD ↔ ARMED без pid_hal_arm_begin, журнал, предикат у журналі й лічильнику затримки | wiring | Агент | M | T5, T6, T11 | done | COMMIT |
| T13 | Підключити в main старт tof і колбеки консолі tof_show та tof_reinit | wiring | Агент | S | T8, T11, T12 | done | COMMIT |
| T14 | Посилити хостові перевірки: повний прогін, статичні перевірки core, пінів, затримок і калібрування для фази 6 | tests | Агент | S | T13 | todo | — |
| T15 | Написати acceptance.md: процедура приймання фази 6 на залізі для Розробника і шаблон записів у журнал вимірювань | docs | Агент | S | T13 | todo | — |

**Total:** 15 задач, ≈ 11.5 людино-днів.
