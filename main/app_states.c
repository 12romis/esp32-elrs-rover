#include "app_states.h"

const char *app_state_name(app_state_t s)
{
    switch (s) {
    case APP_STATE_BOOT:          return "BOOT";
    case APP_STATE_SELFTEST:      return "SELFTEST";
    case APP_STATE_DISARMED:      return "DISARMED";
    case APP_STATE_ARMED:         return "ARMED";
    case APP_STATE_OBSTACLE_HOLD: return "OBSTACLE_HOLD";
    case APP_STATE_FAILSAFE:      return "FAILSAFE";
    case APP_STATE_FAULT:         return "FAULT";
    }
    return "?";
}
