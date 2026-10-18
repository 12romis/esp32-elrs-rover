#!/usr/bin/env bash
# Перевірка AC-25 (phase2-io, constitution пп. 2, 5, 14): у main/ і components/ (крім board)
#  1) немає літералів розрядів розширювача в компонентах, що працюють з розширювачем (ioexp, ui, tof) і в main:
#     «1 << 3», «1u << 5» (розряди лише з board_pins.h);
#  2) немає застарілих драйверів IDF v6.1: driver/i2c.h, driver/adc.h, driver/timer.h, driver/pcnt.h;
#  3) швидкість шини не вище 100000 Гц (scl_speed_hz).
# Використання: test/host/check_bus_rules.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
bad=0
while IFS= read -r f; do
    rel="${f#$ROOT/}"
    hit=""
    case "$rel" in
        main/*|components/ioexp/*|components/ui/*|components/tof/*) hit="$(grep -nE '\b1[uUlL]*[[:space:]]*<<[[:space:]]*[0-9]+' "$f")" ;;
    esac
    [ -n "$hit" ] && { echo "ЛІТЕРАЛ РОЗРЯДУ у $rel (розряди лише з board_pins.h):"; echo "$hit" | sed 's/^/    /'; bad=1; }
    hit="$(grep -nE '#[[:space:]]*include[[:space:]]*[<"]driver/(i2c|adc|timer|pcnt)\.h[>"]' "$f")"
    [ -n "$hit" ] && { echo "ЗАСТАРІЛИЙ ДРАЙВЕР у $rel (v6.1: i2c_master, esp_adc, gptimer, pulse_cnt):"; echo "$hit" | sed 's/^/    /'; bad=1; }
    while IFS= read -r line; do
        num="$(echo "$line" | sed -E 's/.*scl_speed_hz[[:space:]]*=[[:space:]]*([0-9]+).*/\1/')"
        if [ -n "$num" ] && [ "$num" -gt 100000 ] 2>/dev/null; then
            echo "ШВИДКІСТЬ ШИНИ у $rel: scl_speed_hz = $num (максимум 100000)"; bad=1
        fi
    done < <(grep -E 'scl_speed_hz[[:space:]]*=[[:space:]]*[0-9]+' "$f")
done < <(find "$ROOT/main" "$ROOT/components" -type f \( -name '*.c' -o -name '*.h' \) ! -path '*/components/board/*' 2>/dev/null)
[ $bad -eq 0 ] && echo "check_bus_rules: чисто"
exit $bad
