---
id: T17
title: "Прибрати апаратні AC із T12 і T15, додати AC-05 і AC-06 у T10, оновити acceptance.md (review N1)"
layer: "docs"
deps: []
acs: ["AC-20"]
files_hint: ["docs/features/phase5-pid/tasks.json", "docs/features/phase5-pid/tasks/t10-acceptance-checklist.md", "docs/features/phase5-pid/tasks/t12-review-side-view-disarmed.md", "docs/features/phase5-pid/tasks/t15-review-remote-stop-tests.md", "docs/features/phase5-pid/acceptance.md"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20-r2.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T17 — Прибрати апаратні AC із T12 і T15, додати AC-05 і AC-06 у T10, оновити acceptance.md (review N1)

Рев'ю N1 (раунд 2): T12 (`done`) заявляє AC-06, T15 (`done`) — AC-14, хоча AC-20 відносить їх до приймання на залізі. `acceptance.md:3` пише, що AC-05 і AC-06 закриває T10, але в `acs` T10 їх немає. Прибрати AC-06 з T12 і AC-14 з T15, додати AC-05 і AC-06 в T10, оновити кількість тестів у `acceptance.md`.

**DoD:** У `acs` done-задач немає AC-01, -02, -05, -06, -10, -13, -14; `acs` T10 містить AC-05 і AC-06; `acceptance.md` узгоджений з `tasks.json`
