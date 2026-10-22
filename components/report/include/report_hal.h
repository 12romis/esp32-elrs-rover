// HAL report (phase8-session-report, ADR-0003): задача report (core 0, низький пріоритет). Збирає підсумки сесій у чергу
// звітів (NVS), на вході в DISARMED піднімає Wi-Fi на коротку спробу передачі й вимикає його одразу. Озброєння перериває спробу.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "report_queue_core.h"

// Стан для консолі й status (копія під коротким блокуванням).
typedef struct {
    report_q_t queue;
    bool busy;                       // спроба триває (Wi-Fi піднято)
    bool config_ok;                  // налаштування мережі задано
    report_outcome_t last_outcome;
    uint32_t last_outcome_ms;        // мітка (мс від старту) завершення останньої спроби
    uint32_t commits;                // записів у флеш від старту
} report_status_t;

// Після storage_init: відновлює чергу з NVS, піднімає номер сесії, стартує задачу report. Збій лише вимикає звіти.
esp_err_t report_start(void);

void report_hal_status(report_status_t *s);

// Команда `report`: рішення за станом, зайнятістю, чергою й налаштуваннями; при REPORT_GO запускає спробу негайно.
report_gate_t report_hal_attempt_now(void);

// Пояснення відсутніх налаштувань мережі для консолі (без значень).
const char *report_hal_config_problem(void);
