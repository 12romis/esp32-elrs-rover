// HAL сховища звітів у NVS (phase8-session-report, ADR-0004): два слоти документів і метадані (номер наступної сесії) у
// просторі `report`. Один запис — один nvs_commit (блоб документа разом з метаданими); видалення — теж один коміт. Усі
// записи виконує задача report у DISARMED чи FAILSAFE (мотори вимкнені), не такт керування.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "report_queue_core.h"

// Читає слоти й метадані після storage_init. Недійсні слоти (версія, CRC) ігноруються й не ламають чергу. *next_seq —
// max(метадані, найбільший номер у слотах + 1). Помилка NVS — код помилки, черга порожня.
esp_err_t report_store_init(report_q_t *q, uint32_t *next_seq);

// Зберігає документ нового звіту: вільний слот, а коли обидва зайняті — слот найстаршого звіту. Метадані: next_seq.
esp_err_t report_store_put(uint32_t seq, session_end_t reason, const char *json, size_t len, uint32_t next_seq);

// Читає документ звіту з номером seq; *len — довжина без завершального нуля (buf завершується нулем).
esp_err_t report_store_load(uint32_t seq, char *buf, size_t n, size_t *len);

// Видаляє слот доставленого звіту.
esp_err_t report_store_remove(uint32_t seq);

// Скільки комітів у флеш виконано від старту (для приймання: ≤ 2 на сесію).
uint32_t report_store_commits(void);
