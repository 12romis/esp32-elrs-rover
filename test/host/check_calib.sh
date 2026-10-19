#!/usr/bin/env bash
# Перевірка phase4-encoders AC-11 (constitution пп. 3, 13): кожна числова константа калібрувального заголовка має
# в docs/measured.md рядок, що називає її ім'я (`ІМ'Я`), з тим самим значенням. Константа без рядка або з іншим
# значенням — дефект: число вигадане або журнал вимірювань не оновлено.
# Список заголовків — CALIB_FILES (інші *_calib.h додаються сюди, коли їхні константи матимуть рядки з ім'ям).
# Використання: test/host/check_calib.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
CALIB_FILES=("components/encoders/include/encoders_calib.h")
MEASURED="$ROOT/docs/measured.md"
bad=0

# Нормалізація: без пробілів, зірочок, дужок і суфікса u; юнікодний мінус «−» → «-».
norm() { sed -e 's/−/-/g' -e 's/[*{}() ]//g' -e 's/\([0-9]\)u\b/\1/g'; }

if [ ! -f "$MEASURED" ]; then
    echo "check_calib: немає ${MEASURED#$ROOT/}"; exit 1
fi
for rel in "${CALIB_FILES[@]}"; do
    f="$ROOT/$rel"
    [ -f "$f" ] || { echo "check_calib: немає $rel"; bad=1; continue; }
    while IFS= read -r line; do
        name="$(printf '%s' "$line" | sed -E 's/^#[[:space:]]*define[[:space:]]+([A-Za-z0-9_]+).*/\1/')"
        value="$(printf '%s' "$line" | sed -E 's/^#[[:space:]]*define[[:space:]]+[A-Za-z0-9_]+[[:space:]]+//; s#//.*##' | norm)"
        [ -z "$value" ] && continue
        rows="$(grep -F "\`$name" "$MEASURED" | norm)"
        if [ -z "$rows" ]; then
            echo "КОНСТАНТА БЕЗ ЗАМІРУ: $name = $value ($rel) — у docs/measured.md немає рядка з \`$name\`"; bad=1; continue
        fi
        # Значення має стояти окремим числом або списком, а не частиною іншого числа (44 ≠ 144, 2772 ≠ 27720).
        esc="$(printf '%s' "$value" | sed -e 's/[.+]/\\&/g')"
        if ! printf '%s\n' "$rows" | grep -qE "(^|[^0-9.])${esc}([^0-9.]|$)"; then
            echo "ЗНАЧЕННЯ РОЗХОДИТЬСЯ: $name = $value ($rel), а в docs/measured.md:"
            grep -F "\`$name" "$MEASURED" | cut -c1-160 | sed 's/^/    /'; bad=1
        fi
    done < <(grep -E '^#[[:space:]]*define[[:space:]]+[A-Za-z0-9_]+[[:space:]]+[-+({0-9]' "$f")
done
[ $bad -eq 0 ] && echo "check_calib: чисто"
exit $bad
