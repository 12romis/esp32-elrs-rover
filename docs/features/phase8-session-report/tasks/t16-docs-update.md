---
id: T16
title: "Оновити README, архітектурну діаграму й самоперевірку: режим CH9, індикатор, звіт сесії, налаштування мережі, відомі обмеження"
layer: "docs"
deps: ["T14"]
blocks: ["T17"]
acs: []
files_hint: ["README.md", "docs/architecture.md", "docs/self-check.md", "docs/measured.md"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "todo"
step: ""
---

# T16 — Оновити README, архітектурну діаграму й самоперевірку: режим CH9, індикатор, звіт сесії, налаштування мережі, відомі обмеження

## Place in the sequence

- **Blocked by:** T14 — Додати в консоль команду report та поля status про режим контролю й звіти, з підключенням у main · **Blocks:** T17 — Скласти чекліст приймання на залізі для Розробника: CH9, індикатор, список датчиків, доставка, черга, переривання озброєнням, FAILSAFE, замір запису в памʼять · **Wave:** 10 (Єдина задача без `acs`: документаційна; покриття AC закривають T0–T15.).
- **Lane:** own lane.

## Why (user story)

> **As a** Розробник
> **I want** щоб після роззброєння ровер сам надсилав на сервер підсумок сесії
> **So that** я бачу історію виїздів і стан акумулятора без підключення USB
>
> — `spec.md §4, US-04, verbatim` · full text: [spec.md](../spec.md)


> **As a** Розробник
> **I want** щоб невідправлені звіти зберігались і йшли пізніше
> **So that** відсутність мережі чи збій сервера не стирають дані виїзду
>
> — `spec.md §4, US-05, verbatim` · full text: [spec.md](../spec.md)

Задача дає частину історії: оновити README, архітектурну діаграму й самоперевірку: режим CH9, індикатор, звіт сесії, налаштування мережі, відомі обмеження.

## Inlined context

> A19. **Секрети.** SSID і пароль Wi-Fi та пароль сервера зберігаються у локальному файлі налаштувань компонента, який у `.gitignore`; у репозиторії лише шаблон-приклад без значень. Збірка без цього файлу не ламається, а передача вимикається з одним пояснювальним рядком в журналі. Пароль не потрапляє в консоль, `status`, журнал, звіт і в git. Пароль, що прозвучав у чаті,: рекомендовано замінити його справжнім лише у локальному файлі й вважати чат-значення скомпрометованим (OQ-1).
>
> — `spec.md §1, A19, verbatim` · full text: [spec.md](../spec.md)

> Wi-Fi біля приймача ELRS 2.4 ГГц може погіршити радіоканал пульта під час спроби (DISARMED) — ризик Medium; розмір образу після Wi-Fi/TLS; помилково призначений CH9 (значення 992 = «вимкнено») вимкне захист; запис флешу зупиняє виконання коду з флешу на обох ядрах.
>
> — `sad.md §11, чотири ризики, abridged` · full text: [sad.md](../sad.md)

> Конституція, п. 10: канонічні файли проєкту не змінюються без явної згоди Розробника (`sdkconfig.defaults`, кореневий `CMakeLists.txt`, `.vscode/`); у цій фазі Розробник дозволив правки лише `sdkconfig.defaults` і кореневого `CMakeLists.txt` (2026-10-22, spec A22). П. 11: `kicad-rover/` не редагується.
>
> — `docs/constitution.md, пп. 10–11; spec.md §1, A22, abridged` · full text: [constitution.md](../../../constitution.md)


**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [contracts/](../contracts/) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

_Задача документаційна: власних критеріїв із spec §5 немає; покриття AC закривають T0–T15._

## Checklist

- [ ] `README.md`: канал CH9 і налаштування тумблера в EdgeTX, кольори індикатора, команда `report`, як створити `report_secrets.h` з прикладу й де взяти кореневий сертифікат, що передає звіт (поля — посилання на `contracts/events.md`).
- [ ] `docs/architecture.md`: нові задачі й потоки (control → session → report, NVS, Wi-Fi лише DISARMED); `docs/self-check.md`: рядки про безпеку мережі з посиланнями на докази.
- [ ] `docs/measured.md` не редагується Агентом: нагадати Розробнику записати положення CH9 (OQ-6) і TP3 під Wi-Fi (OQ-5).

## Edge cases

| Case | Behaviour |
|---|---|
| замір CH9/TP3 ще не записано | README каже «не виміряно», не вигадує значення (п. 3 конституції) |

## Definition of Done

- [ ] документи не обіцяють більше, ніж перевірено; кожен пункт self-check має посилання на доказ
- [ ] кожне Hard Rule вище лишається дійсним
