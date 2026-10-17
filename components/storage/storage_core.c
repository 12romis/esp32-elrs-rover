// Політика сховища: коли очищати, а коли вважати старт невдалим. Чиста логіка.
#include "storage.h"

storage_action_t storage_decide(storage_result_t first)
{
    switch (first) {
    case STORAGE_RESULT_OK:
        return STORAGE_ACTION_USE;
    case STORAGE_RESULT_NO_FREE_PAGES:
    case STORAGE_RESULT_NEW_VERSION:
        return STORAGE_ACTION_ERASE_AND_RETRY;
    case STORAGE_RESULT_OTHER:
        break;
    }
    return STORAGE_ACTION_FAIL;
}

storage_status_t storage_final_status(storage_result_t first, storage_result_t retry)
{
    switch (storage_decide(first)) {
    case STORAGE_ACTION_USE:
        return STORAGE_STATUS_OK;
    case STORAGE_ACTION_ERASE_AND_RETRY:
        return (retry == STORAGE_RESULT_OK) ? STORAGE_STATUS_WIPED : STORAGE_STATUS_UNAVAILABLE;
    case STORAGE_ACTION_FAIL:
        break;
    }
    return STORAGE_STATUS_UNAVAILABLE;
}

const char *storage_status_text(storage_status_t s)
{
    switch (s) {
    case STORAGE_STATUS_OK:          return "сховище в порядку";
    case STORAGE_STATUS_WIPED:       return "сховище очищено";
    case STORAGE_STATUS_UNAVAILABLE: return "сховище недоступне";
    }
    return "?";
}
