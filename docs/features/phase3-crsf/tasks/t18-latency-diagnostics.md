---
id: T18
title: "Діагностика затримки «кадр → борти» в журналі для NFR ≤ 50 мс (review F5)"
layer: "infra"
deps: []
blocks: []
acs: ["AC-01"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "components/drive/drive_hal.c", "test/host/test_drive.c", "docs/features/phase3-crsf/acceptance.md"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: "—"
---

# T18 — Діагностика затримки «кадр → борти» в журналі для NFR ≤ 50 мс (review F5)

## Source

Знахідка огляду `_review/review-2026-10-18.md` (verdict «Fix now»).

## What

F5: spec §6 вимагає міток прийняття кадру приймальною задачею й запису команди бортам. Накопичувач затримки (кількість, мін, макс, середнє) у drive_core; control раз на 5 с у ARMED з пульта друкує підсумок. Крок виміру в acceptance.md замість оцінки «на око».

## Definition of Done

Тест накопичувача в test_drive.c; крок у acceptance.md. Раннер зелений, збірка без нових попереджень.
