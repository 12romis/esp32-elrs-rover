// Чиста логіка розширювача портів (spec AC-14, AC-15, AC-16; ADR-0003): тіньовий регістр, маска розрядів світла,
// статус доступності, підтвердження запису. Без заголовків ESP-IDF і FreeRTOS.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_pins.h"

// Єдині розряди, які прошивка може підняти до фази 6: світло. Розряди скидання далекомірів і вільний завжди 0.
#define IOEXP_LIGHT_MASK \
    ((uint8_t)((1u << IOEXP_BIT_LIGHT_EN) | (1u << IOEXP_BIT_STOP_RED) | (1u << IOEXP_BIT_STOP_GREEN)))

typedef enum {
    IOEXP_STATUS_UNKNOWN,      // перший запис нуля ще не виконано
    IOEXP_STATUS_AVAILABLE,    // перший запис нуля підтверджено
    IOEXP_STATUS_UNAVAILABLE   // не підтверджено на старті: недоступний до перезапуску плати
} ioexp_status_t;

typedef enum {
    IOEXP_EVT_NONE,
    IOEXP_EVT_FAULT,      // шина щойно перестала відповідати (одне повідомлення в журнал)
    IOEXP_EVT_RECOVERED   // шина відновилась після збою (одне повідомлення в журнал)
} ioexp_event_t;

typedef struct {
    uint8_t shadow;        // копія останнього підтвердженого байта; з шини не зчитується
    ioexp_status_t status;
    bool bus_fault;        // останній запис під час роботи не підтверджено
} ioexp_t;

void ioexp_init(ioexp_t *e);

// Результат першого запису нуля на старті. ack: AVAILABLE, копія 0. Інакше UNAVAILABLE до перезапуску.
void ioexp_start_result(ioexp_t *e, bool ack);

bool ioexp_available(const ioexp_t *e);

// Байт для запису: бажані біти світла під маскою (інші розряди нуль). false — розширювач недоступний.
bool ioexp_prepare(const ioexp_t *e, uint8_t desired, uint8_t *out_byte);

// Чи треба писати: бажаний байт відрізняється від копії або попередній запис не вдався (повтор автоматики).
bool ioexp_needs_write(const ioexp_t *e, uint8_t desired);

// Результат запису байта. ack — копія оновлюється, збій знімається; без ack копія не змінюється.
ioexp_event_t ioexp_commit(ioexp_t *e, uint8_t byte, bool ack);

// Копія розряду світла.
bool ioexp_bit(const ioexp_t *e, int bit);
