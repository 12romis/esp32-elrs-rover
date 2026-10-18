// Політика годування сторожового таймера. Чиста логіка.
#include "watchdog.h"

void watchdog_core_init(watchdog_core_t *w)
{
    w->hang_requested = false;
}

void watchdog_core_request_hang(watchdog_core_t *w)
{
    w->hang_requested = true;
}

bool watchdog_should_feed(const watchdog_core_t *w)
{
    return !w->hang_requested;
}
