---
id: T15
title: "Пояснення FAILSAFE у відмовах і status за причиною (review F2)"
layer: "ports"
deps: []
blocks: []
acs: ["AC-08", "AC-09", "AC-12"]
files_hint: ["components/rover_console/console_core.c", "components/rover_console/include/console_core.h", "components/rover_console/console_hal.c", "components/crsf/crsf_core.c", "components/crsf/include/crsf_core.h", "main/main.c", "test/host/test_console.c", "test/host/test_crsf.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T15 — Пояснення FAILSAFE у відмовах і status за причиною (review F2)

## Source

Знахідка огляду `_review/review-2026-10-18.md` (verdict «Fix now»).

## What

F2: відмова класу 1 у FAILSAFE (arm, drive, reboot, wdt-test, light, beep) для причини «втрата звʼязку» має називати вихід пультом (contracts/cli.md:19), для «напруга» — лише скидання. status показує рядок «вихід з FAILSAFE: …» (drive_fsx_text).

## Definition of Done

Тести test_console.c для обох причин і обох форматерів; test_crsf.c — рядок виходу з FAILSAFE; тексти не обрізаються буферами HAL. Раннер зелений, збірка без нових попереджень.
