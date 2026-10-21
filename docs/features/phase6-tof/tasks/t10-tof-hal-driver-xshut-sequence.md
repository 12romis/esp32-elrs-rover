---
id: T10
title: "Створити tof_hal: драйвер VL53L0X (порт p4_tof.c), послідовність XSHUT з новими адресами, виняток у check_delays"
layer: "infra"
deps: ["T1", "T9"]
blocks: ["T11"]
acs: ["AC-07"]
files_hint: ["components/tof/tof_hal.c", "components/tof/include/tof_hal.h", "components/tof/CMakeLists.txt", "test/host/check_delays.sh"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T10 — Створити tof_hal: драйвер VL53L0X (порт p4_tof.c), послідовність XSHUT з новими адресами, виняток у check_delays

## Place in the sequence

- **Blocked by:** T1 — Створити tof_core: медіана, сектори, зайнятість, звільнення, відмова датчика і калібрувальну константу порогу, T9 — Додати в ioexp_hal операцію зміни лінії скидання і внутрішній мʼютекс запису байта · **Blocks:** T11 — Додати в tof_hal задачу sensors: опитування раз на 50 мс, знімок під portMUX, відмова за 250 мс, tof reinit у DISARMED · **Wave:** 3.
- **Lane:** спільні файли: T1 (`components/tof/CMakeLists.txt`); T11 (`components/tof/include/tof_hal.h`, `components/tof/tof_hal.c`) — серіалізовано.

## Why (user story)

> **As a** Розробник
> **I want** щоб вийнятий, непідключений чи завислий далекомір не зупиняв і не вішав прошивку, а я бачив це в консолі й міг повернути датчик у роботу в DISARMED
> **So that** одна відмова не виводить ровер з ладу, а сліпу зону я бачу і виправляю
>
> — `spec.md §4, US-03, verbatim` · full text: [spec.md](../spec.md)

Ця задача дає код, що піднімає чотири датчики по одному, перепризначає адреси й читає відстань, а відсутній датчик лишає у відмові.

## Inlined context

> - Послідовність старту `tof_hal` (у задачі `sensors`, не в `app_main`): для кожного датчика за порядком U7, U8, U9, U10 підняти його XSHUT через `ioexp_hal_xshut`, пауза 5 мс через `esp_rom_delay_us` (≥ 2 мс за документом 02 §4.7; 5 мс — як у перевіреній послідовності P4, `docs/measured.md`), перевірка ідентифікатора моделі, ініціалізація, перепризначення адреси на 0x30…0x33, безперервний режим; результат кожного друкується в журнал. Розширювач недоступний → усі чотири в відмові з одним попередженням.
>
> — `sad.md §4, журнал припущень design, bullet «Послідовність старту», verbatim` · full text: [sad.md](../sad.md)

> - Транзакції датчика: таймаут 10 мс у циклі опитування (50 мс на ініціалізацію); кожна транзакція під `i2c_bus_lock`; опитування неблокувальне — готового відліку немає, значить, нового відліку цей цикл немає. Після п'яти циклів підряд, у яких не відповів жоден датчик, задача викликає `i2c_bus_reset` (як `ioexp`).
>
> — `sad.md §4, журнал припущень design, bullet «Транзакції датчика», verbatim` · full text: [sad.md](../sad.md)

> Старт іде в задачі `sensors`, а не в `app_main`, тож не затримує вихід у DISARMED. Якщо розширювач недоступний, лінії скидання піднять неможливо, і всі чотири датчики йдуть у відмову з одним попередженням. Інакше датчики піднімаються по одному в порядку U7, U8, U9, U10, і кожен або отримує нову адресу, або потрапляє у відмову з попередженням; жодна з цих відмов не переводить автомат у FAULT. Якщо справних датчиків немає зовсім, попередження повторюється при кожному озброєнні (AC-07).
>
> — `sad.md §6, «Flow 3: старт датчиків і відсутні датчики», потік 3, verbatim` · full text: [sad.md](../sad.md)

> Драйвер VL53L0X — мінімальний власний (рішення D4 закрито в P4); діагностична версія — `tools/bringup/main/p4_tof.c`.
>
> — `sad.md §2, Technical, abridged` · full text: [sad.md](../sad.md)

> **Hard rule:** 14. Шина I²C працює на 100 кГц, підвищувати до 400 кГц заборонено (розділ 4.6 документа `02-FIRMWARE-CONTEXT.md`). Усі періодичні задачі використовують `vTaskDelayUntil`, а не `vTaskDelay` (розділ 8.5).
>
> — `constitution.md, п. 14, verbatim` · full text: [constitution.md](../../../constitution.md)

> **Hard rule:** 2. Номери GPIO існують рівно в одному файлі: `components/board/include/board_pins.h`. Літерал номера піна будь-де ще — дефект, навіть якщо значення правильне.
>
> — `constitution.md, п. 2, verbatim` · full text: [constitution.md](../../../constitution.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/) · [measured.md](../../../measured.md)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-07 (US-03) — error

> **Given** плату вмикають, коли один (в іншому прогоні — усі чотири) далекомір відʼєднано
> **When** Розробник чекає, поки прошивка дійде до DISARMED
> **Then** журнал старту називає результат кожного далекоміра; відсутні названо відмовою з попередженням; автомат не переходить у FAULT і доходить до DISARMED, озброєння не блокується; якщо жоден далекомір не справний, журнал на старті й при кожному озброєнні каже, що захист від зіткнень вимкнено, а ровер їздить без нього
>
> — `spec.md §5, AC-07, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] Створити `include/tof_hal.h`: `tof_start()`, `tof_snapshot_get(tof_view_t*)`, `tof_reinit()` (реалізує T11) — `components/tof/include/tof_hal.h`
- [ ] Портувати з `tools/bringup/main/p4_tof.c` `vl_init`, `vl_start_continuous`, читання відстані (неблокувальне: готовий відлік або ні), `vl_set_address`; кожна транзакція під `i2c_bus_lock`, таймаут 10 мс у циклі, 50 мс в ініціалізації — `components/tof/tof_hal.c`
- [ ] Послідовність: `ioexp_hal_xshut(біт, true)`, `esp_rom_delay_us(5000)`, перевірка моделі на 0x29, ініціалізація, нова адреса 0x30…0x33, безперервний режим; результат кожного датчика в журнал; розширювач недоступний → усі в відмові з одним попередженням — `components/tof/tof_hal.c`
- [ ] Номери бітів XSHUT лише з `board_pins.h` (`IOEXP_BIT_XSHUT_*`) — `components/tof/tof_hal.c`
- [ ] Дозволити `components/tof/tof_hal.c` у `ALLOW` для `vTaskDelay(1)` усередині очікування готовності регістра, з причиною в коментарі — `test/host/check_delays.sh`
- [ ] Оновити `components/tof/CMakeLists.txt`: `SRCS "tof_core.c" "tof_hal.c"`, `PRIV_REQUIRES board ioexp i2c_bus app_state esp_driver_i2c freertos log esp_timer` — `components/tof/CMakeLists.txt`

## Edge cases

| Case | Behaviour |
|---|---|
| Датчик не відповідає на 0x29 | Відмова з попередженням; наступні датчики ініціалізуються |
| Лінія скидання не піднялася (ioexp повернув помилку) | Той самий датчик у відмові, решта продовжується |
| Розширювач недоступний | Усі чотири у відмові, одне попередження, без спроб шини |
| Ідентифікатор моделі не 0xEE | Відмова, адреса не змінюється |
| Два датчики на 0x29 одночасно | Неможливо: лінії скидання піднімаються по одному, решта вже на нових адресах |
| Значення відстані 8190 мм | Відлік «поза діапазоном», не помилка |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень
- [ ] `check_delays.sh`, `check_pins.sh`, `check_core_headers.sh` чисті
- [ ] HAL — приймання на залізі T15 (AC-07); хостового моку немає (пункт 6 конституції)
- [ ] Hard rules пунктів 2 і 14 конституції не порушено
- [ ] every Hard Rule inlined above still holds
