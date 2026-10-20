---
id: T16
title: "Рядок журналу старту з режимом бортів (review F8)"
layer: "wiring"
deps: ["T12"]
acs: ["AC-12"]
files_hint: ["components/drive/drive_core.c", "components/drive/include/drive_core.h", "main/main.c", "test/host/test_drive.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T16 — Рядок журналу старту з режимом бортів (review F8)

Рев'ю F8: AC-12 вимагає, щоб і журнал старту називав борт, який працює без регулятора. Текст будує `drive_core` (`drive_pid_boards_text`) з режиму набору і доступності коліс. `main` пише його один раз після `pid_hal_init`.

**Done when:** хостовий тест тексту зелений, `idf.py build` без попереджень.

**DoD:** `run_tests.sh` зелений з тестом тексту; `idf.py build` без попереджень
