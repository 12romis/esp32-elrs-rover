// Машина станів (docs/02-FIRMWARE-CONTEXT.md §6.3). Чиста логіка: без заголовків ESP-IDF.
#pragma once

typedef enum {
    APP_STATE_BOOT,
    APP_STATE_SELFTEST,
    APP_STATE_DISARMED,
    APP_STATE_ARMED,
    APP_STATE_OBSTACLE_HOLD,
    APP_STATE_FAILSAFE,
    APP_STATE_FAULT,
} app_state_t;

const char *app_state_name(app_state_t s);
