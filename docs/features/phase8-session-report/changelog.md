# Changelog — phase8-session-report

## phase8-session-report — тумблер CH9 для контролю перешкод і звіт сесії після роззброєння

**What:** тумблер пульта (CH9) вмикає й вимикає зупинку ровера перед перешкодою; ввімкнений і справжній захист показує зеленуватий
індикатор плати. Консоль при утриманні називає кожен датчик, що бачить перешкоду. Після кожної сесії (озброєння → вихід з руху) ровер
у DISARMED на коротку спробу (≤ 30 с) вмикає Wi-Fi, публікує один JSON-звіт на MQTT-сервер власника (TLS, закріплений кореневий
сертифікат) і одразу вимикає Wi-Fi. Недоставлені звіти (до двох) лежать у NVS і переживають перезапуск. Нова команда `report`.

**Why:** виїзди потребують підʼїзду впритул до перешкод, діагностики «який датчик бачить» та історії виїздів без USB — [spec](spec.md) §1–§2.
Ключові рішення: [ADR-0001](adr/0001-accumulate-session-statistics-in-the-control-task-and-hand-off-a-summary-through-a-bounded-queue.md)
(статистика в кроці керування), [ADR-0002](adr/0002-implement-a-minimal-mqtt-3-1-1-publisher-in-report-core-over-esp-tls.md) (власний MQTT 3.1.1),
[ADR-0003](adr/0003-bring-wi-fi-up-and-down-per-delivery-attempt-in-a-dedicated-core-0-task-that-yields-to-arming.md) (Wi-Fi лише в DISARMED),
[ADR-0004](adr/0004-persist-unsent-reports-in-nvs-as-a-two-slot-ring-written-by-the-report-task.md) (кільце з двох слотів NVS),
[ADR-0005](adr/0005-gate-tof-occupancy-in-drive-core-by-the-ch9-switch-with-default-on.md) (CH9, за замовчуванням увімкнено),
[ADR-0006](adr/0006-keep-network-secrets-in-an-ignored-header-and-pin-the-owner-root-certificate.md) (секрети поза git).

**How to use:** CH9 вище середини (> 992) — контроль увімкнено; поки пульта немає чи зв'язок втрачено — увімкнено. Налаштування мережі:
скопіювати `components/report/report_secrets.example.h` у `report_secrets.h` (у `.gitignore`), покласти `components/report/certs/root_ca.pem`,
`idf.py fullclean`. `report` показує чергу, режим і мережу, у DISARMED запускає спробу негайно. Поля звіту — [events.md](contracts/events.md),
консоль — [cli.md](contracts/cli.md).

**Operational notes:**
- Config: `sdkconfig.defaults` — розділ застосунку 1.5 МБ (`SINGLE_APP_LARGE`) і lwip на core 0; видалити згенерований `sdkconfig` і зібрати
  заново; прошивати з таблицею розділів (NVS на місці). Записано в [protected-changes.md](protected-changes.md) (дозвіл Розробника, spec A22).
- Без `report_secrets.h` і сертифіката збірка проходить, передача вимкнена з одним поясненням на вхід у DISARMED.
- Узгоджено хостові тести й `check_power_limit.sh` з комітом `bc4aedd` (межа ШІМ 950, поріг ToF 190 мм) окремим комітом.
- Відкрито: OQ-5 (TP3 під Wi-Fi), OQ-6 (положення CH9; AC-01…05 не приймаються без заміру), OQ-7 (переривання озброєнням під час TCP/DNS).
- Rollback: повернути гілку/коміти фази; дані NVS простору `report` ігноруються попередньою прошивкою.

**Acceptance criteria delivered (хостові докази; на залізі — за [acceptance.md](acceptance.md)):** AC-01…AC-04, AC-08, AC-09, AC-11, AC-15,
AC-17, AC-21, AC-25, AC-26 покриті хостовими тестами; AC-05…AC-07 — логіка кольору в `test_ui.c`, колір і ≤ 200 мс — на залізі;
AC-10, AC-12…AC-14, AC-16, AC-18…AC-20, AC-22…AC-24 — реалізовано, перевіряються лише на залізі.
