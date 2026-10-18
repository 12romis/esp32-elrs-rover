---
id: T17
title: "Без підписки control на Task WDT пульт не озброює й мотори не крутяться (review F1)"
layer: "domain"
deps: []
blocks: []
acs: ["AC-01", "AC-03"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "components/drive/drive_hal.c", "test/host/test_drive.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T17 — Без підписки control на Task WDT пульт не озброює й мотори не крутяться (review F1)

## Source

Знахідка огляду `_review/review-2026-10-18.md` (verdict «Fix now»).

## What

F1: коли esp_task_wdt_add не вдався, s_running блокує лише консоль, а крок керування з пульта далі видає ARMED і ШІМ. Додати в вхід кроку прапорець заборони виходів: виходи вимкнені, запит ARMED не видається.

## Definition of Done

Тест test_drive.c: із забороною фронт тумблера не дає ARMED, виходи вимкнені навіть в ARMED; без заборони поведінка незмінна. HAL — перевірка рев'ю. Раннер зелений, збірка без нових попереджень.
