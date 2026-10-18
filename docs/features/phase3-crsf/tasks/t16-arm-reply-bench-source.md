---
id: T16
title: "Відповідь arm у стенді називає джерело «стенд» (review F3)"
layer: "ports"
deps: []
blocks: []
acs: ["AC-13"]
files_hint: ["components/rover_console/console_core.c", "components/rover_console/include/console_core.h", "components/rover_console/console_hal.c", "test/host/test_console.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: "—"
---

# T16 — Відповідь arm у стенді називає джерело «стенд» (review F3)

## Source

Знахідка огляду `_review/review-2026-10-18.md` (verdict «Fix now»).

## What

F3: contracts/cli.md:24 — успішна відповідь arm додатково називає джерело керування «стенд». Текст винести в console_core.

## Definition of Done

Тест test_console.c на текст відповіді. Раннер зелений, збірка без нових попереджень.
