# Changelog — phase0-skeleton

## phase0-skeleton — безпечна основа прошивки: консоль, сторожовий таймер, автомат станів

**What:** прошивка тепер стартує у відомому стані (BOOT → SELFTEST → DISARMED) і друкує кожен перехід; через USB працює консоль з командами `help`, `status`, `reboot`, `wdt-test`; сторожовий таймер задач перевіряється командою навмисного зависання з причиною скидання `TASK_WDT`; автомат має таблицю з 10 дозволених переходів із 49, покриту хостовими тестами. Мотори, ШІМ, STBY, шина периферії та радіо не задіяні.

**Why:** фази 1–3 потребують готової консолі, автомата і сторожового таймера, а помилки в них небезпечні поруч із моторами ([spec](spec.md) §1–§2). Ключові рішення: [ADR-0001](adr/0001-reconfigure-task-wdt-with-panic-at-startup.md) (panic для TWDT програмно, без правки `sdkconfig.defaults`), [ADR-0002](adr/0002-split-phase0-into-app-state-console-watchdog-storage-components.md) (компоненти `app_state`, `rover_console`, `watchdog`, `storage`, кожен core + HAL), [ADR-0003](adr/0003-leave-stby-unconfigured-in-phase0.md) (STBY не налаштовується — виняток із док. 02 §4.5 до фази 1), [ADR-0004](adr/0004-hold-in-boot-when-storage-init-fails.md) (збій сховища → лишаємося в BOOT з консоллю).

**How to use:** прошити плату з VS Code, відкрити монітор. Команди: `help`; `status` (стан, час роботи, вільна внутрішня памʼять, причина скидання, сховище, історія переходів); `reboot` (у DISARMED і BOOT); `wdt-test` (лише DISARMED, перезавантаження ≈5 с, причина `TASK_WDT`). Хостові тести: `test/host/run_tests.sh`; перевірки правил: `test/host/check_*.sh`. Приймання на залізі — [acceptance.md](acceptance.md).

**Operational notes:**
- Migration: <!-- none -->
- Feature flag / config: `sdkconfig.defaults` і корінь `CMakeLists.txt` не змінено; panic для TWDT вмикається в коді.
- Rollback: повернути попередню прошивку (`git revert` гілки / прошити попередній образ); сховище на платі фазою 0 не змінюється, окрім очищення при пошкодженні.
- Відкрите: спосіб відтворити «пошкоджене сховище» на залізі (spec OQ-6), поріг вільної памʼяті (OQ-1). Док. 02 §6.4 (дерево репозиторію) розходиться з фактичним — оновити за окремою вказівкою.

**Acceptance criteria delivered:** AC-01…AC-13 реалізовано й перевірено хостовими тестами (450+ перевірок) та збіркою; AC-01, AC-02, AC-04, AC-07, AC-12 (HAL-частини) чекають приймання на залізі за `acceptance.md`.
