#!/usr/bin/env bash
# Перевірка constitution п. 14: періодичні задачі чекають через vTaskDelayUntil, а не vTaskDelay.
# Шукає vTaskDelay( (без Until) у main/ і components/. Дозволені місця — лише паузи всередині одного вимірювання,
# не період задачі; кожне з причиною в ALLOW.
# Використання: test/host/check_delays.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
# Шлях відносно кореня → причина. battery_hal.c: пауза 1 тік між відліками АЦП в одному пакеті (фаза 1), період
# задачі battery — vTaskDelayUntil. tof_hal.c: vTaskDelay(1) лише в очікуванні готовності регістра VL53L0X в ініціалізації
# (пауза всередині одного вимірювання, phase6-tof); 5 мс після підняття лінії скидання — esp_rom_delay_us, період задачі sensors —
# vTaskDelayUntil.
ALLOW=("components/battery/battery_hal.c" "components/tof/tof_hal.c")
bad=0
while IFS= read -r f; do
    rel="${f#$ROOT/}"
    hit="$(grep -nE '\bvTaskDelay[[:space:]]*\(' "$f")"
    [ -z "$hit" ] && continue
    allowed=0
    for a in "${ALLOW[@]}"; do [ "$rel" = "$a" ] && allowed=1; done
    if [ $allowed -eq 0 ]; then
        echo "vTaskDelay замість vTaskDelayUntil у $rel:"; echo "$hit" | sed 's/^/    /'; bad=1
    fi
done < <(find "$ROOT/main" "$ROOT/components" -type f -name '*.c' 2>/dev/null)
[ $bad -eq 0 ] && echo "check_delays: чисто"
exit $bad
