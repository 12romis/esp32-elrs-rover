---
id: T13
title: "Порівнювати вік кадру й тривалість звʼязку беззнаково (review F6)"
layer: "domain"
deps: []
blocks: []
acs: ["AC-05", "AC-08"]
files_hint: ["components/drive/drive_core.c", "test/host/test_drive.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T13 — Порівнювати вік кадру й тривалість звʼязку беззнаково (review F6)

## Source

Знахідка огляду `_review/review-2026-10-18.md` (verdict «Fix now»).

## What

F6: `(int32_t)(now - t_ms)` у drive_core.c:157 і :195 стає відʼємним, коли кадрів немає ≥ 2^31 мс, і звʼязок вважається живим. Порівнювати як `uint32_t` (control бере now після знімка, тож now ≥ t_ms; sad.md §4 вже каже «різниця беззнакова»).

## Definition of Done

Тест у test_drive.c: вік кадру > 2^31 мс → звʼязок втрачено, FAILSAFE у ARMED; вихід з FAILSAFE не настає через «відʼємну» тривалість. Раннер зелений, `idf.py build` без нових попереджень.
