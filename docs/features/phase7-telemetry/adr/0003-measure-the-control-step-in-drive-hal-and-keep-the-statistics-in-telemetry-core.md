---
status: Accepted
owner: "Розробник проєкту"
reviewers: ["Розробник проєкту"]
updated_at: "2026-10-21"
feature_size: "S"
ticket: "phase7-telemetry"
---

# 0003 — Measure the control step in drive_hal and keep the statistics in telemetry_core

- **Status:** Accepted
- **Date:** 2026-10-21
- **Deciders:** Розробник, Агент (design, рівень easy)

## Context

Документ 02 §5 і §7 просить міряти час такту керування через `esp_timer_get_time()` і показувати його. Задача `control` живе в `drive_hal.c`; консольна команда `status` збирає рядки через колбеки `console_ops_t`, які передає `main`. Поле для часу такту в стандартному кадрі CRSF відсутнє (spec A4), тож вивід лише в `status`. Приклад з фази 3/4: статистика затримки «кадр → борти» (`drive_lat_t`) живе в `drive_core`.

## Decision drivers

- Вимір не додає блокуючих викликів і не змінює періоду (конституція, п. 14; NFR).
- Чиста логіка статистики (перший вимір, найбільше, середнє, перевищення періоду) тестується на хості.
- Роадмап відводить фазі 7 зону `components/telemetry/`.

## Considered options

1. **`drive_hal` бере дві відмітки навколо кроку й передає їх у `telemetry_step_record`; статистика й форматування рядка `status` живуть у `telemetry_core`.**
2. **Статистика в `drive_core` поруч із `drive_lat_t`:** менше залежностей, але розмазує «телеметрію» по компонентах і не використовує зону фази 7.

## Decision outcome

**Chosen:** варіант 1. Відмітки беруться на початку циклу після пробудження й перед `esp_task_wdt_reset` (вимір кроку без очікування наступного періоду). `telemetry_step_record` — короткий критичний блок із чотирма полями; читає `main` для рядка `status` через новий колбек `step_status`. До першого виміру рядок каже «вимірів ще немає».

## Consequences

**Positive**
- Користь для налагодження PID без зміни періоду; чиста логіка тестована.

**Negative**
- `drive` залежить від `telemetry` (один виклик). Цикл залежностей виключений: `telemetry` не залежить від `drive`.

**Neutral**
- Час кроку на пульт не йде (spec §3).

## Links

- Spec: [[../spec.md]] US-02; AC-04, AC-05; A4
- SAD: [[../sad.md]] §5, §8
- Related ADR: phase3-crsf ADR-0003 (мітки часу затримки), ADR-0001 цієї фази
