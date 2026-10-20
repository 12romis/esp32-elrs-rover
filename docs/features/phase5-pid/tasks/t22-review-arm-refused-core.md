---
id: T22
title: "Винести відмову озброєння за ознакою регулятора в drive_core (review S1)"
layer: "app"
deps: []
acs: ["AC-15"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "components/drive/drive_hal.c", "test/host/test_drive.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20-r4.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T22 — Винести відмову озброєння за ознакою регулятора в drive_core (review S1)

Рев'ю S1 (раунд 4): зняття запиту, `accepted` і події та вибір причини SAVE/MODE при відмові дозволу на ARMED живуть у `drive_hal.c` і на хості не перевіряються (конституція п. 6). Винести в `drive_arm_refused(o, have_req, busy)` у `drive_core`; HAL лише викликає.

**DoD:** `run_tests.sh` зелений з тестом `drive_arm_refused` на обидві причини × консоль/пульт; `idf.py build` без попереджень
