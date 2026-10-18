# Tracker — phase3-crsf

> Status of every task in the epic. `implement` updates **Status** and **Step** after every
> subagent it runs (and commits them with the task), so this table shows where the run is right now.
> Status: `todo` · `in_progress` · `blocked` · `review` · `done`.
> Step: `SELECT` · `RED` · `GREEN` · `REFACTOR` · `GATE` · `REVIEW` · `COMMIT` (`—` before the task starts).
> A `blocked` row carries its reason in Step, e.g. `GATE — lint red after 3 retries`.

| # | Task | Layer | Owner | Estimate | Blocked by | Status | Step |
|---|---|---|---|---|---|---|---|
| T1 | Створити crsf_core: збирач кадрів із потоку байтів, CRC8, розпакування 16 каналів, лічильники | domain | Агент (код); Розробник (приймання на залізі) | M | — | done | COMMIT |
| T2 | Доповнити crsf_core: намір пілота (нормалізація, мертва зона, тумблер, функція фронту) і рядки status | domain | Агент (код); Розробник (приймання на залізі) | M | T1 | done | COMMIT |
| T3 | Додати в app_state причину FAILSAFE, підвищення до «напруга» і охорону переходу FAILSAFE → DISARMED | domain | Агент (код); Розробник (приймання на залізі) | S | — | done | COMMIT |
| T4 | Розширити drive_ctl_step: стендовий режим, таймер зв'язку, втрата зв'язку в ARMED і DISARMED, відмова консолі «керує пульт» | domain | Агент (код); Розробник (приймання на залізі) | M | T1, T3 | done | COMMIT |
| T5 | Розширити drive_ctl_step: озброєння фронтом тумблера з умовами, рух стіком, роззброєння тумблером і з консолі | domain | Агент (код); Розробник (приймання на залізі) | M | T2, T4 | done | COMMIT |
| T6 | Розширити drive_ctl_step: вихід з FAILSAFE після ≥ 1 с стабільного зв'язку з тумблером у DISARM і відсічка у FAILSAFE | domain | Агент (код); Розробник (приймання на залізі) | S | T3, T5 | done | COMMIT |
| T7 | Створити crsf_hal: UART2 420000 8N1, задача crsf_rx, знімок під portMUX, Task WDT | infra | Агент (код); Розробник (приймання на залізі) | M | T1 | done | COMMIT |
| T8 | Перевірити ui_core на виході FAILSAFE → DISARMED: блимання, писк і індикатор гаснуть, сигнал переходу звучить | tests | Агент (код); Розробник (приймання на залізі) | S | — | done | COMMIT |
| T9 | Підключити знімок приймача в drive_hal, причину FAILSAFE, журнал подій зв'язку і crsf_start у main | wiring | Агент (код); Розробник (приймання на залізі) | M | T6, T7 | done | COMMIT |
| T10 | Оновити консоль: help для arm і drive, відповіді disarm, рядки status про пульт через колбек rc_status | ports | Агент (код); Розробник (приймання на залізі) | S | T2, T9 | done | COMMIT |
| T11 | Прогнати хостовий раннер і перевірки конституції для phase3-crsf, закрити покриття 49 пар і 126 пар | tests | Агент (код); Розробник (приймання на залізі) | S | T6, T8, T10 | done | COMMIT |
| T12 | Підготувати чекліст приймання фази 3 на залізі і місця для записів у журнал bring-up | docs | Агент (код); Розробник (приймання на залізі) | S | T9, T10, T11 | done | COMMIT |
| T13 | Порівнювати вік кадру й тривалість звʼязку беззнаково (review F6) | domain | Агент (код); Розробник (приймання на залізі) | S | — | done | COMMIT |
| T14 | Показувати в status тумблер «невідомо», коли база фронту невідома (review F4) | domain | Агент (код); Розробник (приймання на залізі) | S | — | todo | — |
| T15 | Пояснення FAILSAFE у відмовах і status за причиною (review F2) | ports | Агент (код); Розробник (приймання на залізі) | S | — | todo | — |
| T16 | Відповідь arm у стенді називає джерело «стенд» (review F3) | ports | Агент (код); Розробник (приймання на залізі) | S | — | todo | — |
| T17 | Без підписки control на Task WDT пульт не озброює й мотори не крутяться (review F1) | domain | Агент (код); Розробник (приймання на залізі) | S | — | todo | — |
| T18 | Діагностика затримки «кадр → борти» в журналі для NFR ≤ 50 мс (review F5) | infra | Агент (код); Розробник (приймання на залізі) | S | — | todo | — |

**Total:** 18 tasks, ~10.5 person-days (T13–T18 — виправлення за оглядом 2026-10-18).
