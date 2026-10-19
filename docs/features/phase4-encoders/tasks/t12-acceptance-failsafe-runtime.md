---
id: T12
title: "acceptance.md: крок AC-03 у FAILSAFE і факт часу хостового прогону (review F3, F4)"
layer: "docs"
deps: []
blocks: []
acs: ["AC-03"]
files_hint: ["docs/features/phase4-encoders/acceptance.md"]
owner: "Агент (документ); Розробник (приймання на залізі)"
estimate: "S"
context_budget: "S"
status: "todo"
step: "—"
---

# T12 — Крок FAILSAFE і час прогону в acceptance.md (review F3, F4)

## Source

Знахідки огляду `_review/review-2026-10-19.md`: F3 (verdict «Fix now», документ), F4 (Defer → spec §8 OQ-2; факт часу прогону — у `acceptance.md`).

## What

- F3: крок приймання — озброїти з пульта, вимкнути пульт (FAILSAFE), прокрутити колесо рукою, `enc`: лічильник змінився, `status` показує FAILSAFE.
- F4: записати виміряний час `test/host/run_tests.sh` з посиланням на OQ-2.

## Definition of Done

`acceptance.md` має крок FAILSAFE з очікуванням і місцем для запису та рядок про час прогону; Агент не позначає приймання виконаним.
