---
id: T11
title: "Створити report_store_hal і налаштування мережі: NVS-кільце звітів одним записом, версія й git образу, локальний файл секретів, сертифікат, зміни сборки"
layer: "infra"
deps: ["T7", "T9"]
blocks: ["T12"]
acs: ["AC-13", "AC-23", "AC-24"]
files_hint: ["components/report/report_store_hal.c", "components/report/include/report_store_hal.h", "components/report/report_secrets.example.h", "components/report/certs/", "components/report/CMakeLists.txt", ".gitignore", "sdkconfig.defaults", "docs/features/phase8-session-report/protected-changes.md"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T11 — Створити report_store_hal і налаштування мережі: NVS-кільце звітів одним записом, версія й git образу, локальний файл секретів, сертифікат, зміни сборки

## Place in the sequence

- **Blocked by:** T7 — Створити report_core: документ звіту сесії у форматі events.md з явними null і межею 1024 байти, T9 — Створити report_queue_core: кільце з двох слотів, порядок передачі, вихід зі спроби при першій невдачі й правила дозволу передачі за станом · **Blocks:** T12 — Створити report_net_hal: підключення Wi-Fi, TLS із закріпленим сертифікатом і обмін MQTT з таймаутом 100 мс на крок · **Wave:** 6.
- **Lane:** shares `components/report/CMakeLists.txt`, `components/report/report_store_hal.c` with T0 — serialized.

## Why (user story)

> **As a** Розробник
> **I want** щоб невідправлені звіти зберігались і йшли пізніше
> **So that** відсутність мережі чи збій сервера не стирають дані виїзду
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: створити report_store_hal і налаштування мережі: NVS-кільце звітів одним записом, версія й git образу, локальний файл секретів, сертифікат, зміни сборки.

## Inlined context

> A17. **Черга звітів.** Невідправлені звіти зберігаються у памʼяті плати (переживають перезапуск і вимкнення живлення), не більше двох; при появі третього найстаріший видаляється з рядком у журналі, який номер видалено. Це не лише кращий буфер: FAILSAFE через напругу виходить лише перезапуском плати, тож у ОЗП такий звіт втратився б. Звіти надсилаються в порядку створення. Успішно надісланий звіт видаляється з памʼяті. Записів у памʼять на сесію не більше двох: створення (у момент завершення сесії) і видалення після підтвердження доставки; плюс оновлення постійного лічильника номерів сесій разом зі створенням (не окремий запис). Усі записи — поза тактом керування. Дублікат, якщо позначка «надіслано» не встигла записатись, відсікається номером сесії на сервері.
>
> — `spec.md §1, A17, verbatim` · full text: [spec.md](../spec.md)

> A18. **FAILSAFE.** Радіо у FAILSAFE не вмикається (низька напруга, піки Wi-Fi, документ 01 §5.4); звіт зберігається в памʼять у момент входу, а надсилається при першому DISARMED: після виходу з FAILSAFE через втрату зв'язку або після перезапуску. 
>
> — `spec.md §1, A18, verbatim` · full text: [spec.md](../spec.md)

> A19. **Секрети.** SSID і пароль Wi-Fi та пароль сервера зберігаються у локальному файлі налаштувань компонента, який у `.gitignore`; у репозиторії лише шаблон-приклад без значень. Збірка без цього файлу не ламається, а передача вимикається з одним пояснювальним рядком в журналі. Пароль не потрапляє в консоль, `status`, журнал, звіт і в git. Пароль, що прозвучав у чаті,: рекомендовано замінити його справжнім лише у локальному файлі й вважати чат-значення скомпрометованим (OQ-1).
>
> — `spec.md §1, A19, verbatim` · full text: [spec.md](../spec.md)

> **Chosen:** варіант 1. Одна операція запису — один `nvs_commit`, у який входять і блоб звіту, і метадані (номер сесії, голова, лічильник); створення й видалення — по одному коміту, разом ≤ 2 на сесію; при завантаженні невалідні слоти ігноруються, а номер відновлюється як максимальний із слотів плюс 1. Третій звіт витісняє найстарший з рядком у журналі. Запис ініціює задача `report` у DISARMED чи FAILSAFE (двигуни вже вимкнені); керування не чекає на запис, але озброєння, що збіглося із записом, затримується на його тривалість (мілісекунди) — як і при `pid save`.
>
> — `adr/0004-persist-unsent-reports-in-nvs-as-a-two-slot-ring-written-by-the-report-task.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> **Chosen:** варіант 1. Пароль ніколи не виводиться: `status` і команда `report` показують «задано / не задано». Сертифікат перевіряється за вбудованим коренем і ім'ям сервера; без перевірки дані не надсилаються (spec AC-22). Кореневий сертифікат публічний за природою, тож може лежати в репозиторії; його файл надає Розробник (OQ-2).
>
> — `adr/0006-keep-network-secrets-in-an-ignored-header-and-pin-the-owner-root-certificate.md, Decision outcome, verbatim` · full text: [adr/](../adr/)

> Конституція, п. 10: канонічні файли проєкту не змінюються без явної згоди Розробника (`sdkconfig.defaults`, кореневий `CMakeLists.txt`, `.vscode/`); у цій фазі Розробник дозволив правки лише `sdkconfig.defaults` і кореневого `CMakeLists.txt` (2026-10-22, spec A22). П. 11: `kicad-rover/` не редагується.
>
> — `docs/constitution.md, пп. 10–11; spec.md §1, A22, abridged` · full text: [constitution.md](../../../constitution.md)

> Таблиця розділів `partitions_singleapp.csv`: застосунок 1 МБ, NVS 24 КБ (0x6000); поточний образ ≈ 371 КБ. Wi-Fi, TLS і MQTT збільшать образ; очікується потреба в більшому розділі застосунку (дозвіл на правки захищених файлів — spec A22).
>
> — `sad.md §2, «Technical», abridged` · full text: [sad.md](../sad.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

Не БД: два слоти звітів ≤ 1024 байти й метадані в просторі NVS `report` — ADR-0004 (`adr/0004-…`, §Decision outcome).

## API contract

Внутрішнє: формат слотів NVS і файл налаштувань; зовнішніх змін немає.

## Acceptance criteria

### AC-13 — cross-context

> **Given** сесію завершив FAILSAFE через напругу, вихід із якого можливий лише перезапуском плати
> **When** ровер перезапускається і входить у DISARMED
> **Then** звіт цієї сесії з причиною «напруга» збережений у памʼяті плати, надсилається при цьому першому DISARMED, а під час самого FAILSAFE Wi-Fi не вмикався
>
> — `spec.md §5, AC-13, verbatim` · full text: [spec.md](../spec.md)

### AC-23 — authorization

> **Given** локальний файл із паролями заповнений
> **When** Розробник вводить будь-яку команду консолі чи дивиться журнал, `status` і звіт на сервері
> **Then** пароль Wi-Fi і пароль сервера ніде не показані, а в репозиторії є лише шаблон без значень
>
> — `spec.md §5, AC-23, verbatim` · full text: [spec.md](../spec.md)

### AC-24 — error handling

> **Given** локального файлу з паролями немає
> **When** збирається прошивка й ровер входить у DISARMED зі звітом у черзі
> **Then** збірка проходить без помилок і нових попереджень, передача не починається, а консоль один раз на кожен вхід у DISARMED із непорожньою чергою пояснює, що налаштування мережі не задано
>
> — `spec.md §5, AC-24, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `report_store_hal.c`: простір NVS `report`; два блоби слотів (версіонований формат, як `PID_RECORD_VERSION`) і блоб метаданих (`next_seq`, голова, лічильник); створення = один `nvs_commit` (блоб + метадані), видалення після доставки = один `nvs_commit`; відновлення при завантаженні: невалідні слоти ігноруються, `next_seq` = максимум зі слотів + 1; перший виклик після `storage_init`.
- [ ] Версія/git/IDF/MAC: `esp_app_get_description()` (version, `idf_ver`), MAC з `esp_read_mac`; `fw.dirty` лише якщо збірка його дає.
- [ ] `report_secrets.example.h` (SSID, пароль Wi-Fi, адреса, порт, ім'я користувача, пароль — порожні) у git; `report_secrets.h` у `.gitignore`; `certs/root_ca.pem` — кореневий сертифікат власника (надає Розробник); `components/report/CMakeLists.txt`: `EMBED_TXTFILES` лише `if(EXISTS …)`, `__has_include` у коді; без файлів збірка проходить, передача вимкнена з одним рядком.
- [ ] `sdkconfig.defaults` (дозволено, spec A22): більший розділ застосунку (`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y`) і прив'язка Wi-Fi/lwip до core 0; кожну правку записати в `docs/features/phase8-session-report/protected-changes.md`; `check_protected.sh` запускати з `ALLOW_PROTECTED=1`.
- [ ] Пароль нігде не друкується: лише «задано/не задано».

## Edge cases

| Case | Behaviour |
|---|---|
| FAILSAFE через напругу | звіт у NVS до перезапуску; після нього перший DISARMED його надсилає |
| метадані втрачені, слоти цілі | `next_seq` відновлюється з максимуму слотів + 1 |
| слот пошкоджений | ігнорується, не ламає чергу |
| немає `report_secrets.h` чи сертифіката | збірка проходить, передача вимкнена, один пояснювальний рядок на вхід у DISARMED із непорожньою чергою |

## Definition of Done

- [ ] запис/читання кільця перевірено на залізі (T17) і хостовими тестами логіки (T9)
- [ ] `idf.py build` без нових попереджень, також без файлу секретів
- [ ] `.gitignore` містить `components/report/report_secrets.h`; секретів у git немає
- [ ] кожне Hard Rule вище лишається дійсним
