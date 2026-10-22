---
id: T7
title: "Створити report_core: документ звіту сесії у форматі events.md з явними null і межею 1024 байти"
layer: "domain"
deps: ["T6"]
blocks: ["T9", "T11"]
acs: ["AC-11", "AC-21"]
files_hint: ["components/report/report_core.c", "components/report/include/report_core.h", "test/host/test_report.c"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "todo"
step: ""
---

# T7 — Створити report_core: документ звіту сесії у форматі events.md з явними null і межею 1024 байти

## Place in the sequence

- **Blocked by:** T6 — Додати в session_core пробіг, час руху, середню й пікову швидкість бортів і дисбаланс із явним «немає даних» · **Blocks:** T9 — Створити report_queue_core: кільце з двох слотів, порядок передачі, вихід зі спроби при першій невдачі й правила дозволу передачі за станом, T11 — Створити report_store_hal і налаштування мережі: NVS-кільце звітів одним записом, версія й git образу, локальний файл секретів, сертифікат, зміни сборки · **Wave:** 4.
- **Lane:** shares `components/report/report_core.c` with T0 — serialized.

## Why (user story)

> **As a** Розробник
> **I want** щоб після роззброєння ровер сам надсилав на сервер підсумок сесії
> **So that** я бачу історію виїздів і стан акумулятора без підключення USB
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: створити report_core: документ звіту сесії у форматі events.md з явними null і межею 1024 байти.

## Inlined context

> A12. **Формат.** Один документ JSON на сесію, укладений у ≤ 1024 байти, послідовні поля зі стабільними назвами англійською (ідентифікатори — англійська, п. 9), одиниці в назвах полів; відсутні дані — явне «немає даних», не нуль. Топіки, QoS і точні назви полів визначає `design`/`api` (Розробник довірив вибір агентові, 2026-10-22).
>
> — `spec.md §1, A12, verbatim` · full text: [spec.md](../spec.md)

> A11. **Версія і ідентифікація.** Версія прошивки й git-хеш беруться з даних образу, які збірка вже вбудовує, версія IDF — з середовища виконання, MAC — з чипа. Прапорець «збірка з незафіксованими змінами» додається, якщо збірка його дає без правки захищених файлів; якщо не дає, його немає (не вигадується).
>
> — `spec.md §1, A11, verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1 (рішення Розробника, 2026-10-22). Кодер/розбірник пакетів — чисті функції, перевіряються хостовими тестами на еталонних байтах; HAL лише відкриває TLS-зʼєднання, пише й читає байти з таймаутом 100 мс на крок (щоб `report` перевіряв стан не рідше ніж раз на 100 мс). Топік `rover/<mac>/session`, QoS 1, `retain` вимкнено, client id `rover-<mac>`, чиста сесія, keepalive не потрібен (зʼєднання коротке); номер сесії у документі відсікає дублікати (spec A12, A17).
>
> — `adr/0002-implement-a-minimal-mqtt-3-1-1-publisher-in-report-core-over-esp-tls.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> Документ — один JSON ≤ 1024 байти; поля англійською з одиницею в назві; відсутнє значення — явний `null`. Поля: `event` (`rover.session.v1`), `schema_version` (1), `seq`, `duration_ms`, `end_reason`, `voltage_start_mv`, `voltage_end_mv`, `voltage_min_mv`, `holds{count,total_ms,min_mm,min_sensor,by_sensor{left_front,front,right_front,rear}}`, `tof_control_off_ms`, `distance_cm`, `moving_ms`, `speed{left_avg_rpm,left_peak_rpm,right_avg_rpm,right_peak_rpm,imbalance_pct}`, `fw{version,git,dirty?,idf}`, `mac`. `fw.dirty` опускається, якщо збірка його не дає.
>
> — `contracts/events.md, «Payload», abridged` · full text: [events.md](../contracts/events.md)

> Конституція, п. 6: кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`; логіка живе в core і покривається тестами на хості. Пп. 8–9: збірка без помилок і без нових попереджень; коментарі й коміти — українською, ідентифікатори — англійською.
>
> — `docs/constitution.md, пп. 6, 8, 9, abridged` · full text: [constitution.md](../../../constitution.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Подія `rover.session.v1` на каналі `rover/<mac>/session` (QoS 1, `retain` вимкнено): повне поле-в-поле див. events.md; ця задача будує лише тіло.

## Acceptance criteria

### AC-11 — cross-context

> **Given** сесія завершилась і звіт готовий до відправлення
> **When** Розробник дивиться на вміст звіту на сервері
> **Then** звіт містить напругу на старті й на роззброєнні, причину завершення сесії (роззброєння, втрата зв'язку чи напруга), кількість утримань перед перешкодою з сумарним часом, мінімальною відстанню і датчиком, пробіг і час руху, середню й пікову швидкість лівого та правого борту з дисбалансом, версію прошивки, git-хеш, версію IDF і MAC плати
>
> — `spec.md §5, AC-11, verbatim` · full text: [spec.md](../spec.md)

### AC-21 — error handling

> **Given** у одного борту немає жодного доступного енкодера, або ровер за сесію не їхав по прямій
> **When** формується звіт
> **Then** швидкості недоступного борту й дисбаланс позначені як «немає даних», а не нулем, і звіт це пояснює
>
> — `spec.md §5, AC-21, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `report_core.h/.c`: `size_t report_core_build_json(char *buf, size_t n, const session_summary_t *s, const report_fw_info_t *fw)` — детермінований порядок полів, числа як ціле/`%.1f`, невалідні значення → `null`, повертає довжину або 0 при переповненні буфера/розмірі > 1024.
- [ ] `report_fw_info_t` (version, git, idf, mac, dirty + прапорець наявності) заповнює HAL (T11/T13); core лише форматує.
- [ ] Хостові тести `test/host/test_report.c`: повний звіт; звіт із усіма `null`; розмір найбільшого випадку ≤ 1024; відсутній `fw.dirty`; екранування рядків; приклади з events.md збігаються байт у байт у компактній формі.

## Edge cases

| Case | Behaviour |
|---|---|
| усі дані відсутні | усі відповідні поля `null`, документ валідний |
| буфер менший за потрібний | повертається 0, нічого не виходить за буфер |
| `fw.dirty` недоступний | поле відсутнє, не `false` |
| довга версія чи хеш | обрізаються до фіксованої межі, документ ≤ 1024 байти |

## Definition of Done

- [ ] хостові тести report проходять; `report_core.c` без заголовків ESP-IDF
- [ ] `idf.py build` без нових попереджень
- [ ] кожне Hard Rule вище лишається дійсним
