---
id: T10
title: "Причина недоступності — вказівник на статичний рядок замість копії, стан енкодерів < 200 байтів (review F5)"
layer: "domain"
deps: ["T9"]
blocks: []
acs: ["AC-08"]
files_hint: ["components/encoders/encoders_core.c", "components/encoders/include/encoders_core.h", "components/encoders/encoders_hal.c", "test/host/test_encoders.c"]
owner: "Агент"
estimate: "S"
context_budget: "S"
status: "todo"
step: "—"
---

# T10 — Причина недоступності без копії рядка (review F5)

## Source

Знахідка огляду `_review/review-2026-10-19.md`, F5 (verdict «Fix now», код).

## What

`char reason[64]` у кожному колесі робить `enc_state_t` ≈ 408 байтів, а sad.md §4 записує «знімок < 200 байтів під `portMUX`». Причина зберігається як `const char *` на рядок статичного часу життя (назви `esp_err_to_name` і літерали HAL), текст формується при друку. HAL перевіряє розмір на цілі: `_Static_assert(sizeof(enc_state_t) < 200, …)`.

## Definition of Done

Хостові тести недоступності зелені, причина NULL дає «невідома причина»; збірка `idf.py build` проходить зі статичною перевіркою розміру, без нових попереджень.
