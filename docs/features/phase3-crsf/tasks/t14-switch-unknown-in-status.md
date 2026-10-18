---
id: T14
title: "Показувати в status тумблер «невідомо», коли база фронту невідома (review F4)"
layer: "domain"
deps: []
blocks: []
acs: ["AC-12"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "components/drive/drive_hal.c", "components/drive/include/drive_hal.h", "main/main.c", "test/host/test_drive.c"]
owner: "Агент (код); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: "—"
---

# T14 — Показувати в status тумблер «невідомо», коли база фронту невідома (review F4)

## Source

Знахідка огляду `_review/review-2026-10-18.md` (verdict «Fix now»).

## What

F4: crsf_intent без кадрів повертає DISARM, тож status у стенді пише «тумблер: DISARM». Показ тумблера брати з бази фронту крок керування (`sw_base`): UNKNOWN у стенді й після втрати звʼязку.

## Definition of Done

Тест через drive_ctl_step: стенд → UNKNOWN; живий звʼязок з тумблером ARM → ARM; після втрати → UNKNOWN. Раннер зелений, збірка без нових попереджень.
