// Документ звіту сесії (phase8-session-report, contracts/events.md, ADR-0002): компактний JSON `rover.session.v1` ≤ 1024 байти.
// Відсутні дані — явний null, не нуль (spec A12, AC-21). Без заголовків ESP-IDF: перевіряється на хості.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "session_core.h"

#define REPORT_JSON_MAX 1024     // межа розміру документа (spec §6)
#define REPORT_VERSION_LEN 32    // довжина буфера версії прошивки та IDF разом із нулем; довші значення обрізаються
#define REPORT_GIT_LEN 16        // git-хеш
#define REPORT_MAC_LEN 13        // 12 hex-символів у нижньому регістрі

// Дані образу й плати: заповнює HAL; core лише форматує.
typedef struct {
    char version[REPORT_VERSION_LEN];
    char git[REPORT_GIT_LEN];
    char idf[REPORT_VERSION_LEN];
    char mac[REPORT_MAC_LEN];
    bool dirty_known;     // збірка повідомила про незафіксовані зміни: тільки тоді поле fw.dirty є в документі
    bool dirty;
} report_fw_info_t;

// Будує документ у buf. Повертає довжину (без завершального нуля) або 0, якщо буфер замалий чи документ більший за
// REPORT_JSON_MAX; у цьому разі buf[0] = '\0' (коли n > 0).
size_t report_core_build_json(char *buf, size_t n, const session_summary_t *s, const report_fw_info_t *fw);

// Назва причини завершення сесії у документі: disarm | failsafe_link_lost | failsafe_voltage.
const char *report_end_reason_name(session_end_t r);

// Українська назва причини для консолі: «роззброєння» | «втрата зв'язку» | «напруга».
const char *report_end_reason_text(session_end_t r);

// Назва датчика в документі за tof_id_t: left_front | front | right_front | rear; NULL поза діапазоном.
const char *report_sensor_name(int sensor);

// Розбір рядка версії образу (esp_app_desc.version, зазвичай `git describe --always --dirty`) на git-хеш і ознаку «dirty».
// Хеш — голий (`b667905`) або після `-g` (`v1.0-3-gb667905`); суфікс `-dirty` — незафіксовані зміни. Якщо рядок не схожий на
// git-версію, git лишається порожнім (у документі «unknown»), а dirty_known = false: ознака не вигадується (spec A11).
void report_fw_from_version(report_fw_info_t *fw, const char *version);
