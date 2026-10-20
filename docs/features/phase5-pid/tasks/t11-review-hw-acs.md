---
id: T11
title: "Прибрати апаратні AC із задач T2–T5 і узгодити acceptance.md (review F5)"
layer: "docs"
deps: []
acs: ["AC-20"]
files_hint: ["docs/features/phase5-pid/tasks.json", "docs/features/phase5-pid/tasks/", "docs/features/phase5-pid/acceptance.md"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T11 — Прибрати апаратні AC із задач T2–T5 і узгодити acceptance.md (review F5)

Рев'ю F5 (`_review/review-2026-10-20.md`): T2–T5 позначено `done`, хоча вони заявляли апаратні AC. AC-20 забороняє закривати такі задачі до приймання на залізі. Рішення Розробника — прибрати апаратні AC з `acs` задач T2–T5. Ці AC лишаються в T10, де приймаються на залізі.

**Done when:** AC-01, AC-02, AC-05, AC-10, AC-13, AC-14 зникли з `acs` задач T2–T5 (і в `tasks.json`, і в `tasks/*.md`); вступ `acceptance.md` узгоджено.

**DoD:** у `tasks.json` і `tasks/t2…t5` немає AC-01, AC-02, AC-05, AC-10, AC-13, AC-14; вони лишаються в T10; `acceptance.md` узгоджено
