#!/usr/bin/env bash
# Перевірка гігієни секретів звіту сесії (phase8-session-report, ADR-0006; spec A19, AC-23; QG-3):
#  1) локальний файл налаштувань мережі report_secrets.h — у .gitignore і не відстежується git;
#  2) шаблон report_secrets.example.h існує й не містить значень рядкових налаштувань;
#  3) жодне значення з локального report_secrets.h (непорожні рядки) не трапляється у відстежуваних файлах;
#  4) у відстежуваних файлах немає заповнених REPORT_WIFI_*/REPORT_MQTT_* (крім порожніх шаблонних);
#  5) каталог certs/ не містить приватних ключів (сертифікат кореня публічний).
# Використання: test/host/check_secrets.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
DIR="components/report"
SECRETS="$DIR/report_secrets.h"
EXAMPLE="$DIR/report_secrets.example.h"
bad=0
cd "$ROOT" || exit 2

if ! git check-ignore -q "$SECRETS" 2>/dev/null; then
    echo "report_secrets.h НЕ в .gitignore: $SECRETS"; bad=1
fi
if git ls-files --error-unmatch "$SECRETS" >/dev/null 2>&1; then
    echo "report_secrets.h ВІДСТЕЖУЄТЬСЯ git (має бути лише локально): $SECRETS"; bad=1
fi
if [ ! -f "$EXAMPLE" ]; then
    echo "немає шаблону: $EXAMPLE"; bad=1
else
    filled="$(grep -E '^#[[:space:]]*define[[:space:]]+REPORT_(WIFI|MQTT)_[A-Z_]+[[:space:]]+"[^"]' "$EXAMPLE")"
    if [ -n "$filled" ]; then
        echo "У ШАБЛОНІ є значення (мають бути порожні рядки):"; echo "$filled" | sed -E 's/"[^"]+"/"…"/' | sed 's/^/    /'; bad=1
    fi
fi

# Значення з локального файлу не повинні потрапити у відстежувані файли.
if [ -f "$SECRETS" ]; then
    while IFS= read -r value; do
        [ ${#value} -lt 3 ] && continue
        hits="$(git ls-files -z | xargs -0 grep -lF -e "$value" 2>/dev/null | grep -vxF "$SECRETS")"
        if [ -n "$hits" ]; then
            echo "ЗНАЧЕННЯ З report_secrets.h ТРАПЛЯЄТЬСЯ у відстежуваних файлах (саме значення не друкується):"
            echo "$hits" | sed 's/^/    /'; bad=1
        fi
    done < <(grep -E '^#[[:space:]]*define[[:space:]]+REPORT_(WIFI|MQTT)_(SSID|PASSWORD|HOST|USER)[[:space:]]+"[^"]' "$SECRETS" \
             | sed -E 's/^[^"]*"([^"]*)".*/\1/')
fi

# Заповнені налаштування у будь-якому відстежуваному файлі.
tracked_filled="$(git ls-files -z | xargs -0 grep -nE '^#[[:space:]]*define[[:space:]]+REPORT_(WIFI|MQTT)_(SSID|PASSWORD|HOST|USER)[[:space:]]+"[^"]' 2>/dev/null)"
if [ -n "$tracked_filled" ]; then
    echo "ЗАПОВНЕНІ НАЛАШТУВАННЯ МЕРЕЖІ У ВІДСТЕЖУВАНОМУ ФАЙЛІ:"; echo "$tracked_filled" | sed -E 's/"[^"]+"/"…"/' | sed 's/^/    /'; bad=1
fi

# Приватні ключі в каталозі сертифікатів.
if [ -d "$DIR/certs" ] && grep -rlE 'PRIVATE KEY' "$DIR/certs" >/dev/null 2>&1; then
    echo "У $DIR/certs є приватний ключ (там має бути лише публічний кореневий сертифікат)"; bad=1
fi

[ $bad -eq 0 ] && echo "check_secrets: чисто"
exit $bad
