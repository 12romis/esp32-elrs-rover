---
id: T9
title: "Написати README (опис, збірка, запуск, інтерфейси) і блок-схему в docs/architecture.md"
layer: "docs"
deps: ["T8"]
blocks: ["T10"]
acs: ["AC-17"]
files_hint: ["README.md", "docs/architecture.md"]
owner: "Агент"
estimate: "M"
context_budget: "S"
status: "done"
step: "GATE (без коміту: CLAUDE.md)"
---

# T9 — Написати README (опис, збірка, запуск, інтерфейси) і блок-схему в docs/architecture.md

## Place in the sequence

- **Blocked by:** T8 — Підключити в main старт telemetry і колбек step_status, показати колір індикатора й час кроку в status · **Blocks:** T10 — Скласти чекліст самоперевірки docs/self-check.md за таблицею документа 02 §5 із посиланнями на докази · **Wave:** 5.
- **Lane:** власна лінія.

## Why (user story)

> **As a** Розробник
> **I want** README з описом, запуском і інтерфейсами, блок-схему та чекліст самоперевірки з посиланнями на докази
> **So that** я (чи інша людина) розумію, що ровер вміє, як його зібрати, і що з цього справді перевірено на залізі
>
> — `spec.md §4, US-06, verbatim` · full text: [spec.md](../spec.md)

Задача дає проєкту опис і схему, за якими нову людину можна ввести за годину.

## Inlined context

> A13. Документація: `README.md` (опис, збірка й прошивка через розширення, хостові тести, інтерфейси: команди консолі, канали пульта, телеметрія, індикація станів, посилання на `board_pins.h`), блок-схема в `docs/architecture.md` (місце за документом 02 §6.4), чекліст у `docs/self-check.md` за таблицею документа 02 §5. Відкладені приймання фаз 1–6 фіксуються в чеклісті як «не перевірено на залізі» і тут не закриваються.
>
> — `spec.md §1, A13, verbatim` · full text: [spec.md](../spec.md)

> **Critical flow 2: вхід у утримання, блимання, вихід**
>
> — `sad.md §6, «Critical flow 2», abridged` · full text: [sad.md](../sad.md)

> ├── telemetry/            (було: заглушка)
>
> — `sad.md §5, «Internal decomposition», verbatim` · full text: [sad.md](../sad.md)

> **Фаза 7 (документ 02 §7).** «README, блок-схема, інструкція запуску, опис інтерфейсів».
>
> — `docs/02-FIRMWARE-CONTEXT.md §7, «Фаза 7», abridged` · full text: [docs/02-FIRMWARE-CONTEXT.md](../../../02-FIRMWARE-CONTEXT.md)

**Fallback:** insufficient or contradicted by the code → read the named file in full ([spec.md](../spec.md) · [sad.md](../sad.md) · [adr/](../adr/)) and follow it. Do not guess.

## Data delta

No DB changes.

## API contract

Internal — no API surface.

## Acceptance criteria

### AC-17 — happy path

> **Given** Розробник вперше відкриває проєкт
> **When** він читає README і блок-схему
> **Then** бачить опис проєкту, як зібрати, прошити й запустити хостові тести, перелік інтерфейсів (команди консолі з правами за станом, канали пульта, телеметрію, індикацію станів із жовтим утриманням) і блок-схему, на якій видно задачі й потоки даних між ними та автомат станів з OBSTACLE_HOLD
>
> — `spec.md §5, AC-17, verbatim` · full text: [spec.md](../spec.md)

## Checklist

- [x] `README.md`: опис, структура каталогів, збірка й прошивка (розширення ESP-IDF, агент лише збирає), хостові тести (`test/host/run_tests.sh`), інтерфейси: команди консолі з правами за станом, канали пульта, телеметрія (нулі = «немає даних»), індикація станів з жовтим утриманням, посилання на `board_pins.h` і журнал вимірювань
- [x] `docs/architecture.md`: блок-схема Mermaid (задачі й потоки даних, автомат станів з OBSTACLE_HOLD); перевірити `mmdc`
- [x] Усі твердження зіставити з кодом і журналом; нічого, чого немає в прошивці

## Edge cases

| Case | Behaviour |
|---|---|
| Опис розходиться з кодом | правий код; розбіжність виправити в документі |
| Діаграма не парситься | виправити до завершення задачі |

## Definition of Done

- [x] README і `docs/architecture.md` існують, діаграма відрендерилась без помилок
- [x] кожне Hard Rule вище лишається дійсним (інваріанти п. 7 конституції не зачеплено)
- [x] збірка без нових попереджень
