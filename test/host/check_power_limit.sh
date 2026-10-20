#!/usr/bin/env bash
# Перевірка phase5-pid AC-09, AC-19: межа потужності — рівно 750 проміле (DRIVE_PWM_MAX_PERMILLE у drive_core.h), і
# жоден інший файл компонентів не визначає власну межу ШІМ. Підняття межі винесено з фази 5 разом із захистом від
# заклинення (spec §3): зміна числа без нового кроку — дефект.
# Використання: test/host/check_power_limit.sh [корінь_проєкту]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
HDR="components/drive/include/drive_core.h"
bad=0

if [ ! -f "$ROOT/$HDR" ] || ! grep -qE '^#[[:space:]]*define[[:space:]]+DRIVE_PWM_MAX_PERMILLE[[:space:]]' "$ROOT/$HDR"; then
    echo "МЕЖІ ПОТУЖНОСТІ НЕМАЄ: $HDR має визначати DRIVE_PWM_MAX_PERMILLE 750"; bad=1
else
    value="$(grep -E '^#[[:space:]]*define[[:space:]]+DRIVE_PWM_MAX_PERMILLE[[:space:]]' "$ROOT/$HDR" | head -1 \
             | sed -E 's/^#[[:space:]]*define[[:space:]]+DRIVE_PWM_MAX_PERMILLE[[:space:]]+//; s#//.*##; s/[[:space:]()u]//g')"
    if [ "$value" != "750" ]; then
        echo "МЕЖА ПОТУЖНОСТІ ЗМІНЕНА: DRIVE_PWM_MAX_PERMILLE = $value ($HDR), у фазі 5 має бути 750 (spec AC-09)"; bad=1
    fi
fi

# Інші визначення межі ШІМ у компонентах (будь-яке ім'я з PWM_MAX), окрім самої DRIVE_PWM_MAX_PERMILLE.
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    echo "ДРУГА МЕЖА ПОТУЖНОСТІ: ${hit#$ROOT/}"; bad=1
done < <(grep -rnE '^#[[:space:]]*define[[:space:]]+[A-Za-z0-9_]*PWM_MAX[A-Za-z0-9_]*[[:space:]]' "$ROOT/components" 2>/dev/null \
         | grep -vE "$HDR:[0-9]+:#[[:space:]]*define[[:space:]]+DRIVE_PWM_MAX_PERMILLE[[:space:]]")

[ $bad -eq 0 ] && echo "check_power_limit: чисто"
exit $bad
