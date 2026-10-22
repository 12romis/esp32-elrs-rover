# API sync report — phase8-session-report

> Форма контракту: `cli.md` (консоль, канал пульта, індикатор) і `events.md` (документ звіту, MQTT-подія), бо `target_surfaces: [cli]` (прочитано з sad.md). OpenAPI-контракту немає: HTTP-інтерфейсу в цій фазі немає (spec §3). `data-model.md` відсутній: **легальний пропуск** (змін схеми БД немає; два слоти звітів у NVS описує ADR-0004, формат документа — `events.md`).

## Field origins (events.md → документ `rover.session.v1`)

| Поле | Origin | Confidence |
|---|---|---|
| `event` | api: ім'я події за конвенцією `module.action.vN` | derived (convention) |
| `schema_version` | **api: додано контрактом, у spec/SAD немає** | inferred — потребує згоди |
| `seq`, `duration_ms` | spec A5 | spec |
| `end_reason` | spec A6 (три значення) | spec |
| `voltage_start_mv`, `voltage_end_mv`, `voltage_min_mv` | spec A7, запит п. 4.1; `null` при застарілому знімку — виведено з `battery_snapshot_usable` | spec / inferred (`null`) |
| `holds.count`, `holds.total_ms`, `holds.min_mm`, `holds.min_sensor` | spec A8, запит п. 4.3 | spec |
| `holds.by_sensor` | spec A8 + clarify (датчики нижче порога на вході) | spec |
| `tof_control_off_ms` | spec A8 | spec |
| `distance_cm`, `moving_ms` | spec A9 | spec |
| `speed.*` | spec A10 | spec |
| `fw.version`, `fw.git`, `fw.idf`, `mac` | spec A11, запит п. 4.6 | spec |
| `fw.dirty` | spec A11 (умовно: лише якщо збірка дає) | spec |
| `holds.*.sensor` імена (`left_front`, `front`, `right_front`, `rear`) | `tof_id_t` у `components/tof` (ліворуч-вперед, прямо, праворуч-вперед, позаду) | existing code |

## Forward checks

| Перевірка | Результат |
|---|---|
| Кожна команда й подія контракту має origin у spec | ✓ (`report` — A16; `status` — A1, A3, A15; журнал — AC-01…AC-24) |
| Розмір документа ≤ 1024 байти (spec §6) | ✓ найбільший приклад у events.md — менше 700 байт у компактній формі; максимум із `fw.dirty` і довгим `fw.version` лишається в запасі (потребує перевірки хостовим тестом) |
| Конвенція: ідентифікатори англійською, одиниці в назвах (spec A12) | ✓ |
| «Немає даних» = `null`, не нуль (spec AC-21) | ✓ для швидкостей, дисбалансу, пробігу, напруги, мінімуму утримань |
| Паролі ніде не друкуються (AC-23) | ✓ у cli.md (лише «задано / не задано») і events.md (поля відсутні) |
| Матриця прав консолі збігається зі spec (AC-17) | ✓ |
| Топік, QoS, client id збігаються з ADR-0002 і SAD §8 | ✓ |

## Back-feed (coverage)

| AC | Де відображено | Статус |
|---|---|---|
| AC-01…AC-04 | cli.md: канал CH9, `status` (контроль), рядки журналу | ✓ |
| AC-05…AC-07 | cli.md: таблиця індикатора, рядки «захист не діє/відновлено» | ✓ |
| AC-08, AC-09 | cli.md: рядок OBSTACLE_HOLD | ✓ |
| AC-10, AC-11, AC-25 | events.md: payload і доставка | ✓ |
| AC-12, AC-13 | events.md: `end_reason`; sad.md потік 3 | ✓ |
| AC-14…AC-16, AC-22 | events.md: retry/DLQ; cli.md: результати спроб | ✓ |
| AC-17 | cli.md: `report`, права за станом | ✓ |
| AC-18, AC-19 | cli.md: журнал (перервано озброєнням); радіо вимкнене поза DISARMED — SAD §8 | ✓ |
| AC-20, AC-21 | events.md: `distance_cm`, `speed.*`, `null` | ✓ |
| AC-23 | cli.md, events.md | ✓ (перевіряється пошуком при прийманні) |
| AC-24 | cli.md: рядок журналу, `report` | ✓ |
| AC-26 | не контракт (хостові тести й збірка) | N/A |

Кожна операція (`report`, `status`, події журналу, документ) прив'язана до US і ≥ 1 AC. Прогалин у послідовностях (§6) немає: усі гілки (немає мережі, сертифікат, відмова, переривання озброєнням, FAILSAFE, немає налаштувань) мають відображення в контракті.

## Знахідки

| # | Знахідка | Клас | Рішення |
|---|---|---|---|
| 1 | Поле `schema_version` не має origin у spec: додано контрактом для еволюції схеми (споживач ігнорує невідомі поля) | flag | Accept за замовчуванням; Розробник може викреслити одним повідомленням (тоді версія лише в імені події) |
| 2 | Значення `null` для напруги при застарілому знімку — виведено, у spec A7 прямо не сказано | flag | Accept; якщо Розробник проти — напруга без знімка не публікується взагалі |
| 3 | Імена датчиків (`left_front`, `front`, `right_front`, `rear`) — англійські ідентифікатори, у консолі — українські (U7…U10 за `tof_sensor_name`) | flag | Accept; відповідність фіксується в README |

Core-знахідок немає; дрейфу між контрактом, spec і SAD не виявлено.

## Lint

`spectral` не застосовується (немає OpenAPI). Для events.md: два JSON-приклади валідні (перевірено розбором), розмір у межах spec §6.
