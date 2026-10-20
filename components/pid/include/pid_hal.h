// HAL регулятора швидкості (phase5-pid; ADR-0002, ADR-0003): поточний набір під portMUX, запис у памʼять плати (NVS),
// задача pid_trace. Логіка набору, запису й текстів — у pid_core.c.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "pid_core.h"

// Читає запис набору з памʼяті плати (викликати після storage_init). Повний правильний запис — набір і режим із
// нього; інакше «регулятор не налаштований» і рядок журналу з причиною (spec AC-07). Ровер стартує далі.
void pid_hal_init(void);

// Копія набору і ознаки «триває збереження» для такту керування (spec AC-03, AC-15).
void pid_hal_get(pid_set_t *out, bool *saving);

// Команди консолі: пишуть відповідь у буфер виклику і повертають довжину.
size_t pid_hal_set_param(pid_param_t p, float value, char *buf, size_t n);   // старе → нове, «не збережено»
// Зміна режиму (spec AC-15): під час озброєння, запису чи поза DISARMED — відмова й режим не змінюється;
// неповний набір — відмова з переліком.
size_t pid_hal_set_mode(bool on, char *buf, size_t n);
// Збереження (spec AC-06, AC-08, AC-15): ознака «триває збереження» ставиться до перевірки стану; під час озброєння
// чи зміни режиму, у ARMED чи OBSTACLE_HOLD — відмова; неповний набір — відмова; збій запису — назва помилки,
// набір у сеансі діє далі з позначкою «не збережено».
size_t pid_hal_save(char *buf, size_t n);
size_t pid_hal_show(char *buf, size_t n);                                     // показ стану (spec AC-03)

// Дозвіл на вхід в ARMED (spec AC-15; рев'ю F1): control бере його безпосередньо перед запитом переходу автомата і
// відпускає після. false — триває запис набору чи зміна режиму (що саме — у why): перехід не запитується.
// Збереження чи зміна режиму, що почнуться після переходу, бачать ARMED і відмовляють.
bool pid_hal_arm_begin(pid_busy_t *why);
void pid_hal_arm_end(void);

// Такт керування публікує стан бортів (для показу) і, коли готовий, кадр трасування (ADR-0003).
void pid_hal_publish_view(const pid_side_view_t view[2]);
void pid_hal_publish_trace(const pid_trace_frame_t *frame);

// Трасування: увімкнення з консолі; задача pid_trace (core 0, пріоритет 1, 100 мс) друкує новий кадр у ARMED.
void pid_hal_trace_enable(bool on);
esp_err_t pid_trace_start(void);
