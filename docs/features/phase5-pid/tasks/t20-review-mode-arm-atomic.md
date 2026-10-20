---
id: T20
title: "Зміна режиму pid on/off атомарна з входом в ARMED (review N4)"
layer: "infra"
deps: ["T19"]
acs: ["AC-15"]
files_hint: ["components/pid/pid_core.c", "components/pid/include/pid_core.h", "components/pid/pid_hal.c", "test/host/test_pid.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20-r2.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T20 — Зміна режиму pid on/off атомарна з входом в ARMED (review N4)

Рев'ю N4 (раунд 2): `pid on`/`pid off` перевіряють DISARMED під час розбору команди, а режим записується пізніше — пілот може озброїти між цими точками. За шаблоном F1: зміна режиму тримає ознаку запису (`pid_guard_try_save`), тож озброєння в цей момент відхиляється; стан перевіряється вже під ознакою, і поза DISARMED режим не змінюється.

**DoD:** `run_tests.sh` зелений з тестом відмови зміни режиму поза DISARMED; `idf.py build` без попереджень; гонка закрита оглядом коду
