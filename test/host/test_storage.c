// Тести політики сховища (AC-12): очищення лише за двох результатів, решта — невдалий старт.
#include <string.h>

#include "storage.h"
#include "test_support.h"

static void test_decide(void)
{
    CHECK(storage_decide(STORAGE_RESULT_OK) == STORAGE_ACTION_USE);
    CHECK(storage_decide(STORAGE_RESULT_NO_FREE_PAGES) == STORAGE_ACTION_ERASE_AND_RETRY);
    CHECK(storage_decide(STORAGE_RESULT_NEW_VERSION) == STORAGE_ACTION_ERASE_AND_RETRY);
    CHECK(storage_decide(STORAGE_RESULT_OTHER) == STORAGE_ACTION_FAIL);
}

static void test_final_status(void)
{
    // непошкоджене: без очищення
    CHECK(storage_final_status(STORAGE_RESULT_OK, STORAGE_RESULT_OTHER) == STORAGE_STATUS_OK);
    // очищення успішне
    CHECK(storage_final_status(STORAGE_RESULT_NO_FREE_PAGES, STORAGE_RESULT_OK) == STORAGE_STATUS_WIPED);
    CHECK(storage_final_status(STORAGE_RESULT_NEW_VERSION, STORAGE_RESULT_OK) == STORAGE_STATUS_WIPED);
    // повторна ініціалізація знову невдала
    CHECK(storage_final_status(STORAGE_RESULT_NO_FREE_PAGES, STORAGE_RESULT_OTHER) == STORAGE_STATUS_UNAVAILABLE);
    CHECK(storage_final_status(STORAGE_RESULT_NEW_VERSION, STORAGE_RESULT_NO_FREE_PAGES) == STORAGE_STATUS_UNAVAILABLE);
    // інша помилка: не очищали, недоступне; retry ігнорується
    CHECK(storage_final_status(STORAGE_RESULT_OTHER, STORAGE_RESULT_OK) == STORAGE_STATUS_UNAVAILABLE);
}

static void test_never_erase_healthy(void)
{
    CHECK(storage_decide(STORAGE_RESULT_OK) != STORAGE_ACTION_ERASE_AND_RETRY);
}

static void test_status_text(void)
{
    CHECK(strlen(storage_status_text(STORAGE_STATUS_OK)) > 0);
    CHECK(strlen(storage_status_text(STORAGE_STATUS_WIPED)) > 0);
    CHECK(strlen(storage_status_text(STORAGE_STATUS_UNAVAILABLE)) > 0);
    CHECK(strcmp(storage_status_text(STORAGE_STATUS_OK), storage_status_text(STORAGE_STATUS_WIPED)) != 0);
    CHECK(strcmp(storage_status_text(STORAGE_STATUS_WIPED), storage_status_text(STORAGE_STATUS_UNAVAILABLE)) != 0);
}

int main(void)
{
    test_decide();
    test_final_status();
    test_never_erase_healthy();
    test_status_text();
    return test_summary();
}
