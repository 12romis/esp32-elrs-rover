---
id: T14
title: "Зробити атомарними вхід в ARMED і початок збереження набору (review F1)"
layer: "infra"
deps: ["T13"]
acs: ["AC-15"]
files_hint: ["components/pid/pid_core.c", "components/pid/include/pid_core.h", "components/pid/pid_hal.c", "components/pid/include/pid_hal.h", "components/drive/drive_hal.c", "test/host/test_pid.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T14 — Зробити атомарними вхід в ARMED і початок збереження набору (review F1)

Рев'ю F1: `control` читає ознаку збереження на початку такту, а ARMED запитує в кінці. Консоль може почати збереження між цими точками. Протокол ознак «триває збереження» / «триває озброєння» живе в `pid_core`, а `pid_hal` виконує його під `s_mux`. `drive_hal` перед `app_state_request(ARMED)` бере дозвіл і відпускає його після переходу. Збереження, що почалося після переходу, бачить ARMED і відмовляє.

**Done when:** хостовий тест протоколу зелений. Під час збереження озброєння відхиляється з причиною «триває збереження набору». Під час озброєння збереження відхиляється.

**DoD:** `run_tests.sh` зелений з тестом протоколу ознак; `idf.py build` без попереджень; гонка закрита оглядом коду
