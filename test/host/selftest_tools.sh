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

[ $fail -eq 0 ] && echo "selftest_tools: усе гаразд" || echo "selftest_tools: ЄСТЬ ПОМИЛКИ"
exit $fail
