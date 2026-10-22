// Шаблон налаштувань мережі звіту (phase8-session-report, ADR-0006). Скопіюйте цей файл у `report_secrets.h` (він у .gitignore)
// і заповніть значення. Без `report_secrets.h` збірка проходить, а передача звітів вимкнена з одним поясненням у журналі.
// Паролі нігде не друкуються: консоль і status показують лише «задано / не задано».
#pragma once

#define REPORT_WIFI_SSID ""        // назва домашньої мережі Wi-Fi
#define REPORT_WIFI_PASSWORD ""    // пароль Wi-Fi
#define REPORT_MQTT_HOST ""        // адреса MQTT-сервера (ім'я має збігатися з сертифікатом)
#define REPORT_MQTT_PORT 8883      // порт захищеного з'єднання
#define REPORT_MQTT_USER ""        // ім'я користувача сервера
#define REPORT_MQTT_PASSWORD ""    // пароль сервера
