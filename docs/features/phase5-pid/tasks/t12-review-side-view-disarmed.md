---
id: T12
title: "Показ режиму бортів поза ARMED — з поточного набору, а не із зафіксованого режиму (review F2)"
layer: "app"
deps: []
acs: ["AC-03", "AC-12"]
files_hint: ["components/drive/drive_core.c", "test/host/test_drive.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T12 — Показ режиму бортів поза ARMED — з поточного набору, а не із зафіксованого режиму (review F2)

Рев'ю F2: `side_view` (`drive_core.c`) визначає режим борту через `c->pid_mode`, а той фіксується лише при вході в ARMED. Поза ARMED показ має брати поточний режим набору (`in->pid.mode`); зафіксований режим лишається тільки для керування в ARMED (AC-15, AC-16).

**Done when:** у DISARMED з увімкненим набором `pid_view` показує ONE/TWO/NO_ENC за доступністю коліс; з вимкненим — OPEN. Хостовий тест це перевіряє.

**DoD:** `run_tests.sh` зелений з тестом показу в DISARMED; `idf.py build` без помилок і нових попереджень
