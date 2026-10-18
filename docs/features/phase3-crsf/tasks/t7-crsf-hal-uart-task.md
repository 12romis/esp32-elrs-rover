---
id: T7
title: "Створити crsf_hal: UART2 420000 8N1, задача crsf_rx, знімок під portMUX, Task WDT"
layer: "infra"
deps: ["T1"]
blocks: ["T9"]
acs: ["AC-05", "AC-10"]
files_hint: ["components/crsf/crsf_hal.c", "components/crsf/include/crsf_hal.h", "components/crsf/CMakeLists.txt"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "M"
context_budget: "S"
status: "done"
step: "COMMIT"
---

<!-- To the executing agent: work from what is inlined here. If a slice is insufficient, ambiguous, or contradicts the code in front of you, open the named file for the full text and follow that. Do not invent the missing part. -->

# T7 — Створити crsf_hal: UART2 420000 8N1, задача crsf_rx, знімок під portMUX, Task WDT

## Place in the sequence

- **Blocked by:** T1 — Створити crsf_core: збирач кадрів із потоку байтів, CRC8, розпакування 16 каналів, лічильники · **Blocks:** T9 — Підключити знімок приймача в drive_hal, причину FAILSAFE, журнал подій зв'язку і crsf_start у main · **Wave:** 2.
- **Lane:** спільні файли з T1 — серіалізовано.

## Why (user story)

> **As a** Розробник
> **I want** щоб биті, обрізані, чужі кадри й випадкові байти від приймача відкидалися
> **So that** перешкоди чи перепідключення приймача не рухають ровер, не тримають зв'язок «живим» і не перезавантажують плату
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Тонкий HAL, який подає байти приймача в `crsf_core` і публікує знімок із міткою прийняття.

## Inlined context

> Лінія приймача: UART2, 420000 бод 8N1, RX — `PIN_ELRS_RX`, TX — `PIN_ELRS_TX` (`board_pins.h`; значення виміряні в P5). Драйвер `driver/uart.h` з чергою подій (документ 02 §7 «Фаза 3»); `UART_HW_FIFO_LEN` замість видаленого `UART_FIFO_LEN` (документ 02 §8).
>
> — `sad.md §2, Technical, verbatim` · full text: [sad.md](../sad.md)

> Задача `crsf_rx`: пріоритет 6, ядро 1 (документ 02 §6.1), стек 3072 байти; подієва — чекає на черзі подій UART з таймаутом 100 мс, щоб регулярно скидати Task WDT навіть без пульта; на подіях переповнення FIFO чи буфера скидає вхідний буфер, рахує подію й продовжує.
> Драйвер UART: буфер прийому 512 байтів, без буфера передачі (телеметрія — фаза 7), черга подій на 16 елементів; лінію TX налаштовано, але нічого не передається.
> Мітка прийняття кадру — `esp_timer_get_time()` у мс, яку ставить `crsf_rx` у момент, коли кадр пройшов перевірку довжини й контрольної суми (spec §6).
>
> — `sad.md §4, журнал припущень, abridged` · full text: [sad.md](../sad.md)

> **Hard rule:** Номери GPIO існують рівно в одному файлі: `components/board/include/board_pins.h`. Літерал номера піна будь-де ще — дефект.
>
> — `constitution.md, п. 2, abridged` · full text: [../../constitution.md](../../../constitution.md)

> **Chosen:** варіант 1. Він зберігає одного писача стану й виходів і порядок ADR-0002 фази 1, а вся логіка зв'язку стає частиною чистого кроку `drive_ctl_step` з тестами на підставному годиннику.
>
> — `adr/0001, Decision outcome, abridged` · full text: [adr/0001-decide-link-loss-in-control-task-from-receiver-snapshot.md](../adr/0001-decide-link-loss-in-control-task-from-receiver-snapshot.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/cli.md](../contracts/cli.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-05 — happy path

> **Given** ровер озброєний з пульта й їде (колеса в повітрі)
> **When** Розробник вимикає пульт (або приймач перестає отримувати сигнал)
> **Then** не пізніше ніж через 0.52 с після останнього валідного кадру каналів обидва борти отримують нуль, STBY опускається, автомат переходить у FAILSAFE, консоль називає причину «втрата зв'язку» і час без кадрів; індикація така сама, як у будь-якому FAILSAFE фази 2: задні ліхтарі блимають червоним, індикатор плати червоний, зумер періодично пищить
>
> — `spec.md §5, AC-05, verbatim` · full text: [spec.md](../spec.md)

### AC-10 — error

> **Given** ровер у будь-якому стані (зокрема в ARMED під час руху)
> **When** у лінію приймача надходять випадкові байти, обрізані кадри, кадри з неправильною довжиною чи контрольною сумою, кадри невідомих типів, або приймач перепідключають на ходу
> **Then** такі дані відкидаються й рахуються окремими лічильниками; стан автомата, команди бортам і таймер зв'язку від них не змінюються; плата не перезавантажується; перший валідний кадр каналів після шуму приймається як звичайний
>
> — `spec.md §5, AC-10, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `crsf_hal.h`: `crsf_start()`, `crsf_snapshot_get(snapshot*)`.
- [ ] `crsf_hal.c`: `uart_driver_install` (UART2, RX 512, TX 0, черга 16), `uart_param_config` 420000 8N1, `uart_set_pin` з `PIN_ELRS_TX`/`PIN_ELRS_RX`; помилка будь-якого кроку — рядок журналу, задача не стартує (стенд).
- [ ] Задача `crsf_rx` (prio 6, core 1, стек 3072): `xQueueReceive` з таймаутом 100 мс → `uart_read_bytes` → `crsf_feed` з `now_ms`; переповнення → `uart_flush_input`, лічильник; підписка на Task WDT і скидання на кожній ітерації.
- [ ] Знімок під `portMUX` (копія < 100 байтів).
- [ ] Додати `crsf_hal.c` у `SRCS` і залежності (`esp_driver_uart`, `esp_timer`, `board`) у `components/crsf/CMakeLists.txt`.

## Edge cases

| Case | Behaviour |
|---|---|
| Пульт вимкнено, подій немає | задача прокидається раз на 100 мс і скидає WDT |
| Переповнення FIFO / буфера | буфер скинуто, лічильник +1, таймер зв'язку реагує як на тишу |
| Збій ініціалізації UART | журнал, ровер працює в стенді |

## Definition of Done

- [ ] `idf.py build` без помилок і нових попереджень; `check_pins.sh` чистий
- [ ] Приймання на залізі (T12): лічильники кадрів ростуть ≈ 100/с, 0 % битих
- [ ] every Hard Rule inlined above still holds
- [ ] lint + vet clean (якщо налаштовані; інакше `check_*.sh`)
