---
id: T21
title: "Окрема ознака зміни режиму і своя причина відмови озброєння; актуальні коментарі заголовків (review R1)"
layer: "infra"
deps: []
acs: ["AC-15"]
files_hint: ["components/pid/pid_core.c", "components/pid/include/pid_core.h", "components/pid/pid_hal.c", "components/pid/include/pid_hal.h", "components/drive/drive_core.c", "components/drive/include/drive_core.h", "components/drive/drive_hal.c", "test/host/test_pid.c", "test/host/test_drive.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20-r3.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T21 — Окрема ознака зміни режиму і своя причина відмови озброєння; актуальні коментарі заголовків (review R1)

Рев'ю R1 (раунд 3): зміна режиму `pid on/off` тримає ознаку збереження, тож озброєння, що збіглося з нею, відхиляється з неправдивим текстом «триває збереження набору». Окрема ознака `changing` у `pid_guard_t`; `pid_guard_try_arm` повертає, що саме заважає (`pid_busy_t`); `drive_hal` бере причину `DRIVE_REASON_PID_MODE` / `DRIVE_BLOCK_PID_MODE` («триває зміна режиму регулятора»). Коментарі `pid_hal.h` і `pid_core.h` описують поточну поведінку.

**DoD:** `run_tests.sh` зелений з тестами ознаки зміни режиму і текстів причини; `idf.py build` без попереджень; огляд коду HAL
