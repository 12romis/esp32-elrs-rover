// Тести політики годування (AC-04): годуємо, доки не запитано зависання.
#include "test_support.h"
#include "watchdog.h"

static void test_feeds_by_default(void)
{
    watchdog_core_t w;
    watchdog_core_init(&w);
    CHECK(watchdog_should_feed(&w));
}

static void test_hang_stops_feeding(void)
{
    watchdog_core_t w;
    watchdog_core_init(&w);
    watchdog_core_request_hang(&w);
    CHECK(!watchdog_should_feed(&w));
}

static void test_hang_request_idempotent(void)
{
    watchdog_core_t w;
    watchdog_core_init(&w);
    watchdog_core_request_hang(&w);
    watchdog_core_request_hang(&w);
    CHECK(!watchdog_should_feed(&w));
}

static void test_init_resets_hang(void)
{
    watchdog_core_t w;
    watchdog_core_init(&w);
    watchdog_core_request_hang(&w);
    watchdog_core_init(&w);
    CHECK(watchdog_should_feed(&w));
}

int main(void)
{
    test_feeds_by_default();
    test_hang_stops_feeding();
    test_hang_request_idempotent();
    test_init_resets_hang();
    return test_summary();
}
