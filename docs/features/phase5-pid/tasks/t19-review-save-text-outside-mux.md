---
id: T19
title: "Форматувати відповідь збереження поза критичною секцією (review N3)"
layer: "infra"
deps: ["T18"]
acs: ["AC-08"]
files_hint: ["components/pid/pid_hal.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20-r2.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T19 — Форматувати відповідь збереження поза критичною секцією (review N3)

Рев'ю N3 (раунд 2): `pid_hal_save` викликає `snprintf` з чотирма float під `s_mux`, а `control` щотакту чекає на тому самому spinlock. Під `s_mux` лишити тільки позначку й `s_load`, текст формувати поза секцією.

**DoD:** Під `s_mux` у `pid_hal_save` немає `snprintf`; `idf.py build` без попереджень; огляд коду
