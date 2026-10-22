# Журнал змін захищених файлів — phase8-session-report

> Конституція, п. 10: канонічні файли (`CMakeLists.txt` кореня, `.vscode/`, `sdkconfig.defaults`) не змінюються без явної згоди Розробника.
> Згода: Розробник дозволив правки `sdkconfig.defaults` і кореневого `CMakeLists.txt` 2026-10-22 (spec A22, OQ-3), лише в обсязі,
> потрібному цій фазі. Кожна правка записана тут. `test/host/check_protected.sh` запускається з `ALLOW_PROTECTED=1` на гілці фази.

| Файл | Правка | Навіщо | Задача |
|---|---|---|---|
| `sdkconfig.defaults` | `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y` | Wi-Fi, TLS і MQTT збільшують образ понад 1 МБ; розділ застосунку 1.5 МБ (зміщень NVS немає: `nvs` лишається за `0x9000`) | T11 |
| `sdkconfig.defaults` | `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y` | стек lwip на core 0 разом із Wi-Fi (документ 02 §6.1, ADR-0003); control і CRSF лишаються на core 1 | T11 |
| `CMakeLists.txt` (корінь) | не змінювався | — | — |

Після зміни `sdkconfig.defaults` згенерований `sdkconfig` (не в git) не підхоплює нові значення сам: виконайте `idf.py fullclean`
і видаліть `sdkconfig` (або повну очистку в розширенні ESP-IDF), потім збирайте й прошивайте повторно (оновиться й таблиця розділів).
