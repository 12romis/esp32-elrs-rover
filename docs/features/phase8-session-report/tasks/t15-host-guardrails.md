---
id: T15
title: "Додати хостові перевірки: гігієна секретів у git, повний прогін збірки й тестів, захищені файли дозволені лише в межах фази"
layer: "tests"
deps: ["T14"]
blocks: ["T17"]
acs: ["AC-23", "AC-26"]
files_hint: ["test/host/check_secrets.sh", "test/host/check_core_headers.sh", "test/host/run_tests.sh"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

# T15 — Додати хостові перевірки: гігієна секретів у git, повний прогін збірки й тестів, захищені файли дозволені лише в межах фази

## Place in the sequence

- **Blocked by:** T14 — Додати в консоль команду report та поля status про режим контролю й звіти, з підключенням у main · **Blocks:** T17 — Скласти чекліст приймання на залізі для Розробника: CH9, індикатор, список датчиків, доставка, черга, переривання озброєнням, FAILSAFE, замір запису в памʼять · **Wave:** 10.
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** щоб паролі Wi-Fi і сервера були лише в локальному файлі і ніколи не показувались
> **So that** репозиторій і журнал можна безпечно ділити
>
> — `spec.md §4, US-08, verbatim` · full text: [spec.md](../spec.md)


> **As a** Агент
> **I want** збирати прошивку й запускати хостові тести логіки сесії, статистики, черги й режиму контролю
> **So that** я підтверджую правила без заліза й не пускаю в збірку нові попередження
>
> — `spec.md §4, US-09, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: додати хостові перевірки: гігієна секретів у git, повний прогін збірки й тестів, захищені файли дозволені лише в межах фази.

## Inlined context

> A19. **Секрети.** SSID і пароль Wi-Fi та пароль сервера зберігаються у локальному файлі налаштувань компонента, який у `.gitignore`; у репозиторії лише шаблон-приклад без значень. Збірка без цього файлу не ламається, а передача вимикається з одним пояснювальним рядком в журналі. Пароль не потрапляє в консоль, `status`, журнал, звіт і в git. Пароль, що прозвучав у чаті,: рекомендовано замінити його справжнім лише у локальному файлі й вважати чат-значення скомпрометованим (OQ-1).
>
> — `spec.md §1, A19, verbatim` · full text: [spec.md](../spec.md)

> A22. **Залежності й гілка.** Фаза спирається на фази 3 (канали CRSF), 4–5 (енкодери, швидкість бортів), 6 (далекоміри) і 7 (індикатор плати, рядок утримання), яка злита в `main` (підтверджено Розробником 2026-10-22): реалізація фази 8 стартує від `main`. Правки захищених файлів (`sdkconfig.defaults`, кореневий `CMakeLists.txt`) для Wi-Fi, захищеного з'єднання й сховища дозволені Розробником 2026-10-22 (п. 10), лише в обсязі, потрібному цій фазі, і кожна фіксується в журналі змін фази.
>
> — `spec.md §1, A22, verbatim` · full text: [spec.md](../spec.md)

> | Витік секретів | 0 входжень паролів у git, консоль, журнал, `status`, звіт | пошук по репозиторію й виводу |
> | Збірка | без помилок і нових попереджень (п. 8) | `idf.py build` |
> | Покриття логікою на хості | 0 заголовків ESP-IDF у `*_core.c` нових компонентів; кожне правило A1–A10 і A17 має ≥ 1 хостовий тест (п. 6) | `test/host/` |
>
> — `spec.md §6, три рядки, verbatim` · full text: [spec.md](../spec.md)

> Конституція, п. 10: канонічні файли проєкту не змінюються без явної згоди Розробника (`sdkconfig.defaults`, кореневий `CMakeLists.txt`, `.vscode/`); у цій фазі Розробник дозволив правки лише `sdkconfig.defaults` і кореневого `CMakeLists.txt` (2026-10-22, spec A22). П. 11: `kicad-rover/` не редагується.
>
> — `docs/constitution.md, пп. 10–11; spec.md §1, A22, abridged` · full text: [constitution.md](../../../constitution.md)

> Конституція, п. 6: кожен компонент ділиться на `<name>_core.c` (без жодного заголовка ESP-IDF) і `<name>_hal.c`; логіка живе в core і покривається тестами на хості. Пп. 8–9: збірка без помилок і без нових попереджень; коментарі й коміти — українською, ідентифікатори — англійською.
>
> — `docs/constitution.md, пп. 6, 8, 9, abridged` · full text: [constitution.md](../../../constitution.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-23 — authorization

> **Given** локальний файл із паролями заповнений
> **When** Розробник вводить будь-яку команду консолі чи дивиться журнал, `status` і звіт на сервері
> **Then** пароль Wi-Fi і пароль сервера ніде не показані, а в репозиторії є лише шаблон без значень
>
> — `spec.md §5, AC-23, verbatim` · full text: [spec.md](../spec.md)

### AC-26 — happy path

> **Given** логіка сесії, статистики, черги звітів і режиму контролю реалізована в частинах без заголовків ESP-IDF
> **When** Агент запускає хостові тести й збирає прошивку
> **Then** усі хостові тести проходять, збірка завершується без помилок і без нових попереджень
>
> — `spec.md §5, AC-26, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [ ] `test/host/check_secrets.sh`: `report_secrets.h` у `.gitignore`, не відстежується; `report_secrets.example.h` без значень; у `git ls-files` немає рядків паролів/SSID (пошук за шаблоном із example); сертифікат у `certs/` — публічний.
- [ ] Підключити нові `*_core.c` до `check_core_headers.sh` (вже глобом); перевірити, що `run_tests.sh` знаходить `test_session.c`, `test_report.c`, `test_mqtt.c`, `test_report_queue.c`.
- [ ] Повний прогін: `idf.py build` + `test/host/run_tests.sh` + всі `check_*.sh` (`check_protected.sh` з `ALLOW_PROTECTED=1` і виводом списку змінених захищених файлів — лише `sdkconfig.defaults`/`CMakeLists.txt`).

## Edge cases

| Case | Behaviour |
|---|---|
| `report_secrets.h` потрапив у індекс git | `check_secrets.sh` падає |
| змінено `.vscode/` чи `kicad-rover/` | `check_protected.sh` падає навіть з `ALLOW_PROTECTED=1` для цих шляхів (перевірка списку) |
| новий `*_core.c` підключив заголовок ESP-IDF | `check_core_headers.sh` падає |

## Definition of Done

- [ ] усі `check_*.sh` і `run_tests.sh` проходять; `idf.py build` без нових попереджень
- [ ] кожне Hard Rule вище лишається дійсним
