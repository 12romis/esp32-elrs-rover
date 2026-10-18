#!/usr/bin/env bash
# Перевірка AC-09: файли чистої логіки components/*/*_core.c не підключають заголовків ESP-IDF і FreeRTOS.
# Використання: test/host/check_core_headers.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
bad=0
shopt -s nullglob
for f in "$ROOT"/components/*/*_core.c; do
    hit="$(grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](esp_|freertos/|driver/|nvs|sdkconfig|soc/|hal/|rom/|esp32)' "$f")"
    if [ -n "$hit" ]; then
        echo "ЗАБОРОНЕНИЙ ЗАГОЛОВОК у ${f#$ROOT/}:"; echo "$hit" | sed 's/^/    /'; bad=1
    fi
done
[ $bad -eq 0 ] && echo "check_core_headers: чисто"
exit $bad
