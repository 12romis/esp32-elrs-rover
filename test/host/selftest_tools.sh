#!/usr/bin/env bash
# Самоперевірка скриптів test/host: кожна перевірка має падати на поганому прикладі
# і мовчати на чистому. Фікстури створюються у тимчасовому каталозі.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail=0
expect() { # опис, очікуваний код (0 або nz = рівно 1: «перевірка виявила дефект»), команда...
    local what="$1" want="$2"; shift 2
    "$@" >"$TMP/out" 2>&1; local rc=$?
    if { [ "$want" = 0 ] && [ $rc -ne 0 ]; } || { [ "$want" = nz ] && [ $rc -ne 1 ]; }; then
        echo "FAIL: $what (код $rc)"; sed 's/^/    /' "$TMP/out"; fail=1
    else
        echo "ok:   $what"
    fi
}

# --- run_tests.sh: порожній набір і набір з тестом
mkdir -p "$TMP/empty"
expect "порожній набір тестів падає" nz "$HERE/run_tests.sh" "$TMP/empty"
expect "набір із тестом проходить" 0 "$HERE/run_tests.sh"
# середовище ESP-IDF у PATH (чужий ld) не повинно ламати хостову збірку
mkdir -p "$TMP/.espressif/bin"
printf '#!/bin/sh\necho "неправильний ld" >&2\nexit 1\n' >"$TMP/.espressif/bin/ld"
chmod +x "$TMP/.espressif/bin/ld"
expect "run_tests ігнорує інструменти .espressif у PATH" 0 env PATH="$TMP/.espressif/bin:$PATH" "$HERE/run_tests.sh"

# --- check_core_headers.sh
mkdir -p "$TMP/bad/components/foo" "$TMP/good/components/foo"
printf '#include <stdint.h>\n#include "freertos/FreeRTOS.h"\n' >"$TMP/bad/components/foo/foo_core.c"
printf '#include <stdint.h>\n#include <string.h>\n#include "foo.h"\n' >"$TMP/good/components/foo/foo_core.c"
expect "check_core_headers ловить заборонений заголовок" nz "$HERE/check_core_headers.sh" "$TMP/bad"
expect "check_core_headers мовчить на чистому" 0 "$HERE/check_core_headers.sh" "$TMP/good"

# --- check_bus_rules.sh
mkdir -p "$TMP/bbad1/components/ioexp" "$TMP/bbad2/components/foo" "$TMP/bbad3/components/foo" "$TMP/bgood/components/ioexp" "$TMP/bgood/components/watchdog"
printf 'uint8_t b = 1u << 5;\n' >"$TMP/bbad1/components/ioexp/x_hal.c"
printf '#include "driver/i2c.h"\n' >"$TMP/bbad2/components/foo/foo_hal.c"
printf 'cfg.scl_speed_hz = 400000;\n' >"$TMP/bbad3/components/foo/foo_hal.c"
printf 'uint8_t b = 1u << IOEXP_BIT_STOP_RED;\ncfg.scl_speed_hz = 100000;\n#include "driver/i2c_master.h"\n' >"$TMP/bgood/components/ioexp/x_hal.c"
printf 'mask |= (1U << 0);\n' >"$TMP/bgood/components/watchdog/w_hal.c"
expect "check_bus_rules ловить літерал розряду" nz "$HERE/check_bus_rules.sh" "$TMP/bbad1"
expect "check_bus_rules ловить застарілий драйвер шини" nz "$HERE/check_bus_rules.sh" "$TMP/bbad2"
expect "check_bus_rules ловить швидкість вище 100 кГц" nz "$HERE/check_bus_rules.sh" "$TMP/bbad3"
expect "check_bus_rules мовчить на чистому" 0 "$HERE/check_bus_rules.sh" "$TMP/bgood"

# --- check_pins.sh
mkdir -p "$TMP/pbad/components/foo" "$TMP/pbad/components/board/include" "$TMP/pgood/components/foo" "$TMP/pgood/components/board/include"
printf 'gpio_set_level(21, 1);\n' >"$TMP/pbad/components/foo/foo_hal.c"
printf '#define PIN_STBY 21\n' >"$TMP/pbad/components/board/include/board_pins.h"
printf 'gpio_set_level(PIN_STBY, 1);\n' >"$TMP/pgood/components/foo/foo_hal.c"
for bad in 'gpio_set_level((gpio_num_t)21, 1);' '#define MY_PIN 21' '.pin_bit_mask = 1ULL << 21,'; do
    mkdir -p "$TMP/pbad2/components/foo" "$TMP/pbad2/components/board/include"
    printf '%s\n' "$bad" >"$TMP/pbad2/components/foo/foo_hal.c"
    expect "check_pins ловить: $bad" nz "$HERE/check_pins.sh" "$TMP/pbad2"
done
printf '#define PIN_STBY 21\n' >"$TMP/pgood/components/board/include/board_pins.h"
expect "check_pins ловить літерал піна" nz "$HERE/check_pins.sh" "$TMP/pbad"
expect "check_pins мовчить, коли піни лише в board_pins.h" 0 "$HERE/check_pins.sh" "$TMP/pgood"

# --- check_protected.sh
git init -q -b main "$TMP/repo"
( cd "$TMP/repo" && git config user.email t@t && git config user.name t \
  && echo a >sdkconfig.defaults && echo b >other.txt && git add -A && git commit -qm init )
expect "check_protected мовчить на чистому дереві" 0 "$HERE/check_protected.sh" "$TMP/repo"
echo changed >>"$TMP/repo/sdkconfig.defaults"
expect "check_protected ловить зміну sdkconfig.defaults" nz "$HERE/check_protected.sh" "$TMP/repo"
( cd "$TMP/repo" && git checkout -q -- . && git checkout -q -b feat && echo more >>sdkconfig.defaults && git commit -qam "змінено захищений" )
expect "check_protected ловить закомічену зміну відносно main" nz "$HERE/check_protected.sh" "$TMP/repo"
expect "ALLOW_PROTECTED=1 дозволяє" 0 env ALLOW_PROTECTED=1 "$HERE/check_protected.sh" "$TMP/repo"

# --- check_calib.sh (phase4-encoders T7, AC-11): константи енкодерів мають рядок у docs/measured.md з тим самим значенням
mkdir -p "$TMP/cgood/components/encoders/include" "$TMP/cgood/docs" "$TMP/cbad1/components/encoders/include" "$TMP/cbad1/docs" \
         "$TMP/cbad2/components/encoders/include" "$TMP/cbad2/docs" "$TMP/cbad3/components/encoders/include" "$TMP/cbad3/docs"
CAL='#define ENC_PULSES_PER_REV 2772\n#define ENC_DIR_SIGN { +1, -1, +1, -1 }\n#define WHEEL_DIAMETER_MM 44\n'
MEAS='| `ENC_PULSES_PER_REV` | **2772** імпульси |\n| `ENC_DIR_SIGN[4]` підтверджено | **+1, −1, +1, −1** |\n| `WHEEL_DIAMETER_MM` | **44** мм |\n'
printf "$CAL" >"$TMP/cgood/components/encoders/include/encoders_calib.h"; printf "$MEAS" >"$TMP/cgood/docs/measured.md"
printf "$CAL" | sed 's/WHEEL_DIAMETER_MM 44/WHEEL_DIAMETER_MM 43/' >"$TMP/cbad1/components/encoders/include/encoders_calib.h"; printf "$MEAS" >"$TMP/cbad1/docs/measured.md"
printf "$CAL" >"$TMP/cbad2/components/encoders/include/encoders_calib.h"; printf "$MEAS" | grep -v WHEEL >"$TMP/cbad2/docs/measured.md"
printf "$CAL" | sed 's/+1, -1, +1, -1/+1, +1, +1, -1/' >"$TMP/cbad3/components/encoders/include/encoders_calib.h"; printf "$MEAS" >"$TMP/cbad3/docs/measured.md"
expect "check_calib ловить розходження значення (43 проти 44)" nz "$HERE/check_calib.sh" "$TMP/cbad1"
expect "check_calib ловить константу без рядка в measured.md" nz "$HERE/check_calib.sh" "$TMP/cbad2"
expect "check_calib ловить розходження знаків коліс" nz "$HERE/check_calib.sh" "$TMP/cbad3"
expect "check_calib мовчить на узгоджених значеннях" 0 "$HERE/check_calib.sh" "$TMP/cgood"
expect "check_calib мовчить на репозиторії" 0 "$HERE/check_calib.sh"

# --- check_power_limit.sh (phase5-pid T9, AC-09, AC-19): межа потужності — 750 проміле у drive_core.h, ніде іншим числом
mkdir -p "$TMP/pgood/components/drive/include" "$TMP/pbad1/components/drive/include" "$TMP/pbad2/components/drive/include" \
         "$TMP/pbad2/components/pid" "$TMP/pbad3/components/drive/include"
printf '#define DRIVE_PWM_MAX_PERMILLE 750\n' >"$TMP/pgood/components/drive/include/drive_core.h"
printf '#define DRIVE_PWM_MAX_PERMILLE 800\n' >"$TMP/pbad1/components/drive/include/drive_core.h"
printf '#define DRIVE_PWM_MAX_PERMILLE 750\n' >"$TMP/pbad2/components/drive/include/drive_core.h"
printf 'static const float limit = 0.9f;  // PWM_MAX\n#define PID_PWM_MAX_PERMILLE 900\n' >"$TMP/pbad2/components/pid/pid_core.c"
printf '/* без межі */\n' >"$TMP/pbad3/components/drive/include/drive_core.h"
expect "check_power_limit ловить межу 800 замість 750" nz "$HERE/check_power_limit.sh" "$TMP/pbad1"
expect "check_power_limit ловить другу константу межі в іншому файлі" nz "$HERE/check_power_limit.sh" "$TMP/pbad2"
expect "check_power_limit ловить відсутню межу" nz "$HERE/check_power_limit.sh" "$TMP/pbad3"
expect "check_power_limit мовчить на межі 750" 0 "$HERE/check_power_limit.sh" "$TMP/pgood"
expect "check_power_limit мовчить на репозиторії" 0 "$HERE/check_power_limit.sh"

[ $fail -eq 0 ] && echo "selftest_tools: усе гаразд" || echo "selftest_tools: ЄСТЬ ПОМИЛКИ"
exit $fail
