---
id: T11
title: "Відмова enc називає, що саме не так: невідоме слово, помилка в reset, зайве слово (review F2)"
layer: "ports"
deps: []
blocks: []
acs: ["AC-07"]
files_hint: ["components/rover_console/console_core.c", "components/rover_console/include/console_core.h", "components/rover_console/console_hal.c", "test/host/test_console.c"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "done"
step: "COMMIT"
---

# T11 — Відмова enc називає причину (review F2)

## Source

Знахідка огляду `_review/review-2026-10-19.md`, F2 (verdict «Fix now», код; контракт без змін).

## What

`contracts/cli.md`: «Будь-яка інша форма (невідоме друге слово, помилка в `reset`, третє слово) — клас „хибні аргументи“: відповідь називає, що саме не так, і перелічує обидві допустимі форми». Зараз `console_msg_enc_error()` дає один загальний текст. Розбір `console_parse_enc` повертає причину (за зразком `console_parse_beep`), `console_msg_enc_error(r)` — текст за причиною з переліком обох форм. «Помилка в reset» — слово, що відрізняється від `reset` лише регістром або є його префіксом чи розширенням (`Reset`, `rese`, `resetx`).

## Definition of Done

Тести `test_console.c`: три причини дають три різні тексти, кожен з обома формами; класифікація `console_parse` у всіх станах незмінна. Раннер зелений, `idf.py build` без нових попереджень.
