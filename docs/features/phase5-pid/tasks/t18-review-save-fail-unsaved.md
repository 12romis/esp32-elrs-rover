---
id: T18
title: "Розділити завершення збереження на позначку й текст; збій сховища знімає «збережено» (review N2)"
layer: "domain"
deps: []
acs: ["AC-08"]
files_hint: ["components/pid/pid_core.c", "components/pid/include/pid_core.h", "test/host/test_pid.c"]
owner: "Агент"
estimate: "S"
source: "_review/review-2026-10-20-r2.md"
status: "done"
step: "GATE ✓ — без коміту (рішення Розробника)"
---

# T18 — Розділити завершення збереження на позначку й текст; збій сховища знімає «збережено» (review N2)

Рев'ю N2 (раунд 2): `pid_save_finish` при збої сховища не скидає `saved`, тож після збою `pid` показує «(збережено)», якщо набір був збережений до спроби. AC-08 вимагає позначку «не збережено». Розділити на `pid_save_mark` (рішення про позначку) і `pid_format_save` (текст), щоб HAL міг формувати текст поза критичною секцією (T19).

**DoD:** `run_tests.sh` зелений з тестом збою сховища при стартовому `saved=true`; `check_core_headers.sh` чистий; `idf.py build` без попереджень
