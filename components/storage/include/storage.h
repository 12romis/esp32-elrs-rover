// Політика сховища налаштувань (spec AC-12, ADR-0004). Чиста логіка: без заголовків ESP-IDF.
#pragma once

// Узагальнений результат ініціалізації сховища (HAL переводить esp_err_t у цей тип).
typedef enum {
    STORAGE_RESULT_OK,
    STORAGE_RESULT_NO_FREE_PAGES,  // «немає вільних сторінок»
    STORAGE_RESULT_NEW_VERSION,    // «знайдено новішу версію»
    STORAGE_RESULT_OTHER           // будь-яка інша помилка
} storage_result_t;

typedef enum {
    STORAGE_ACTION_USE,             // сховище готове
    STORAGE_ACTION_ERASE_AND_RETRY, // очистити і ініціалізувати повторно
    STORAGE_ACTION_FAIL             // невдалий старт, сховище не чіпати
} storage_action_t;

typedef enum {
    STORAGE_STATUS_OK,
    STORAGE_STATUS_WIPED,
    STORAGE_STATUS_UNAVAILABLE
} storage_status_t;

// Що робити після першої ініціалізації.
storage_action_t storage_decide(storage_result_t first);

// Підсумковий стан для `status`. retry враховується лише якщо first вимагав очищення.
storage_status_t storage_final_status(storage_result_t first, storage_result_t retry);

const char *storage_status_text(storage_status_t s);
