---
status: Draft
owner: "Розробник проєкту"
reviewers: []
updated_at: "2026-10-22"
feature_size: M
---

# Events — phase8-session-report

Асинхронний контракт для потоків 1–4 із `sad.md` §6. Подія — опублікований факт: один звіт сесії. Контракт **виведено** зі spec (A5–A12, A15–A17, AC-10, AC-11, AC-25) і sad.md §5 («Документ звіту»), ADR-0002, ADR-0004; чужих полів тут немає.

## Channel: `rover/<mac>/session`

- **Producer:** `report` (прошивка ровера); `<mac>` — MAC плати у нижньому регістрі без роздільників (12 hex-символів).
- **Consumers:** сервер власника (зовнішня система); ровер не підписується на жоден топік (spec §3).
- **Delivery:** at-least-once — QoS 1, `retain` вимкнено; повідомлення вважається доставленим, коли сервер підтвердив прийом (spec A15).
- **Ordering:** публікація в порядку створення (від найстарішого невідправленого); сервер впорядковує за `seq`, а не за часом отримання.
- **Client id:** `rover-<mac>`, чиста сесія, keepalive не використовується (зʼєднання коротке, ADR-0002).
- **Автентифікація:** ім'я користувача й пароль із локального файлу налаштувань; сервер перевіряється власним кореневим сертифікатом (ADR-0006). Пароль у документі, журналі й `status` не з'являється.

## Event: `rover.session.v1`

Ім'я події — у полі `event` документа (топік один). Версія схеми — `schema_version`.

### Ідемпотентність і повтори

- **Idempotency-key:** пара (`mac`, `seq`). `seq` — зростаючий `uint32` у памʼяті плати, не повторюється навіть після перезапуску (spec A5, ADR-0004).
- Повторна публікація того самого звіту безпечна: дублікат, якщо підтвердження не дійшло, відсікає сервер за (`mac`, `seq`).
- **Retry:** у межах одного DISARMED повторних спроб немає; наступна спроба — при наступному вході в DISARMED або командою `report` (spec A13, A16). Звіти йдуть по черзі від найстаршого; невдача зупиняє спробу, решта лишається в тому ж порядку (spec clarify).
- **Dead-letter:** не використовується: роль DLQ грає черга в памʼяті плати на ≤ 2 звіти; третій витісняє найстарший, і сервер бачить пропуск у `seq` (spec A17, AC-15).

### Payload (`data`)

Документ — один JSON ≤ 1024 байти; поля англійською з одиницею в назві (spec A12). Відсутнє значення — явний `null`, не нуль; кожне таке поле описано нижче.

| Поле | Тип | Одиниця / зміст | Коли `null` | Джерело |
|---|---|---|---|---|
| `event` | string | константа `rover.session.v1` | ніколи | api (ім'я події) |
| `schema_version` | integer | константа 1 | ніколи | api (додано контрактом; потребує згоди, див. api-sync-report) |
| `seq` | integer | номер сесії, зростає | ніколи | spec A5 |
| `duration_ms` | integer | тривалість сесії, мс | ніколи | spec A5 |
| `end_reason` | string | `disarm` \| `failsafe_link_lost` \| `failsafe_voltage` | ніколи | spec A6 |
| `voltage_start_mv` | integer | напруга на вході в ARMED, мВ | знімок напруги застарів чи відсутній | spec A7, запит п. 4.1 |
| `voltage_end_mv` | integer | напруга на виході, мВ (для `failsafe_voltage` — остання перед відсічкою) | те саме | spec A7 |
| `voltage_min_mv` | integer | мінімум за сесію, мВ | те саме | spec A7 |
| `holds.count` | integer | кількість входів в OBSTACLE_HOLD | ніколи (0, якщо не було) | spec A8 |
| `holds.total_ms` | integer | сумарний час утримання, мс | ніколи | spec A8 |
| `holds.min_mm` | integer | мінімальна відстань на вході (`hold_mm`), мм | немає утримань | spec A8 |
| `holds.min_sensor` | string | датчик, що дав мінімум (`hold_nearest`): `left_front` \| `front` \| `right_front` \| `rear` | немає утримань | spec A8 |
| `holds.by_sensor` | object (4 integer: `left_front`, `front`, `right_front`, `rear`) | у скількох утриманнях датчик був нижче порога на вході | ніколи (нулі) | spec A8, clarify |
| `tof_control_off_ms` | integer | час, коли режим контролю був вимкнений, мс | ніколи | spec A8 |
| `distance_cm` | integer | пробіг, см (на інтервалах ненульової потужності) | немає жодного доступного енкодера | spec A9 |
| `moving_ms` | integer | час руху: хоч один борт з ненульовою потужністю, мс | ніколи | spec A9 |
| `speed.left_avg_rpm`, `speed.right_avg_rpm` | number | середній модуль швидкості борту за час руху, об/хв | борт без доступного енкодера або `moving_ms` = 0 | spec A10 |
| `speed.left_peak_rpm`, `speed.right_peak_rpm` | number | пікова швидкість за сесію, об/хв | те саме | spec A10 |
| `speed.imbalance_pct` | number | (лівий − правий) / півсума, %, плюс = лівий швидший | немає інтервалів прямої їзди чи борт без енкодера | spec A10 |
| `fw.version` | string | версія з даних образу | ніколи (мінімум «unknown») | spec A11 |
| `fw.git` | string | git-хеш образу | ніколи (мінімум «unknown») | spec A11 |
| `fw.dirty` | boolean | збірка з незафіксованими змінами; поле відсутнє, якщо збірка його не дає | поле опускається | spec A11 |
| `fw.idf` | string | версія ESP-IDF | ніколи | spec A11 |
| `mac` | string | MAC плати (12 hex, нижній регістр) | ніколи | spec A11 |

Помилки й порожні значення: поле не нуль, а `null`, щоб «немає даних» відрізнялось від виміряного нуля (spec AC-21). Невідомі поля споживач ігнорує (еволюція без зламу).

### Приклад (значення умовні)

```json
{
  "event": "rover.session.v1",
  "schema_version": 1,
  "seq": 42,
  "duration_ms": 187000,
  "end_reason": "disarm",
  "voltage_start_mv": 8240,
  "voltage_end_mv": 7710,
  "voltage_min_mv": 7480,
  "holds": {"count": 3, "total_ms": 4200, "min_mm": 95, "min_sensor": "front",
            "by_sensor": {"left_front": 1, "front": 3, "right_front": 2, "rear": 0}},
  "tof_control_off_ms": 0,
  "distance_cm": 5320,
  "moving_ms": 142000,
  "speed": {"left_avg_rpm": 88.4, "left_peak_rpm": 141.0, "right_avg_rpm": 84.1,
            "right_peak_rpm": 138.5, "imbalance_pct": 5.0},
  "fw": {"version": "x.y.z", "git": "abcdef0", "idf": "v6.1", "mac": "aabbccddeeff"}
}
```

### Приклад із відсутніми даними

```json
{
  "event": "rover.session.v1", "schema_version": 1, "seq": 43, "duration_ms": 9000,
  "end_reason": "failsafe_voltage",
  "voltage_start_mv": 6100, "voltage_end_mv": 5950, "voltage_min_mv": 5950,
  "holds": {"count": 0, "total_ms": 0, "min_mm": null, "min_sensor": null,
            "by_sensor": {"left_front": 0, "front": 0, "right_front": 0, "rear": 0}},
  "tof_control_off_ms": 0, "distance_cm": null, "moving_ms": 0,
  "speed": {"left_avg_rpm": null, "left_peak_rpm": null, "right_avg_rpm": null,
            "right_peak_rpm": null, "imbalance_pct": null},
  "fw": {"version": "x.y.z", "git": "abcdef0", "idf": "v6.1", "mac": "aabbccddeeff"}
}
```
