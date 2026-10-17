#!/usr/bin/env bash
# Перевірка AC-11: літерали номерів ліній існують лише в components/board/include/board_pins.h.
# Шукає в main/ і components/ (крім board) GPIO_NUM_<n>, GPIO<n> і числовий перший аргумент gpio_*(...).
# Використання: test/host/check_pins.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
bad=0
while IFS= read -r f; do
    hit="$(grep -nE 'GPIO_NUM_[0-9]+|\bGPIO[0-9]+\b|gpio_[a-z_]+\([[:space:]]*[0-9]+' "$f")"
    if [ -n "$hit" ]; then
        echo "ЛІТЕРАЛ ПІНА у ${f#$ROOT/}:"; echo "$hit" | sed 's/^/    /'; bad=1
    fi
done < <(find "$ROOT/main" "$ROOT/components" -type f \( -name '*.c' -o -name '*.h' \) ! -path '*/components/board/*' 2>/dev/null)
[ $bad -eq 0 ] && echo "check_pins: чисто"
exit $bad
