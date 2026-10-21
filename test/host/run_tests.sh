#!/usr/bin/env bash
# Хостовий раннер: збирає кожен test_*.c разом з усіма components/*/*_core.c звичайним gcc,
# запускає і підсумовує. Порожній набір (0 тестів) вважається помилкою.
# Використання: test/host/run_tests.sh [каталог_з_тестами]
set -u
# Середовище ESP-IDF додає в PATH інструменти інших архітектур (наприклад, ld від esp32ulp-elf),
# які ламають хостовий gcc: прибираємо каталоги .espressif із PATH.
PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v '/\.espressif/' | paste -sd: -)"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="${1:-$ROOT/test/host}"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

INCLUDES=()
for d in "$ROOT"/components/*/include; do [ -d "$d" ] && INCLUDES+=("-I$d"); done
INCLUDES+=("-I$ROOT/test/host")
mapfile -t CORES < <(ls "$ROOT"/components/*/*_core.c 2>/dev/null)

# Ядра збираються один раз у об'єктні файли (паралельно), кожен тест — теж паралельно; вивід — у порядку імен (≤ 5 с, phase6-tof NFR).
mkdir -p "$BUILD/obj"
printf '%s\n' "${CORES[@]}" | xargs -P 8 -I{} sh -c 'gcc -std=c11 -Wall -Wextra -Werror '"${INCLUDES[*]}"' -c "{}" -o "'"$BUILD"'/obj/$(basename "{}" .c).o" 2>"'"$BUILD"'/obj/$(basename "{}" .c).log" || { echo "ЗБІРКА ЯДРА ПРОВАЛЕНА: {}"; cat "'"$BUILD"'/obj/$(basename "{}" .c).log"; exit 255; }'
core_rc=$?
run_one() {
    local t="$1" name; name="$(basename "$t" .c)"
    if ! gcc -std=c11 -Wall -Wextra -Werror "${INCLUDES[@]}" -o "$BUILD/$name" "$t" "$BUILD"/obj/*.o 2>"$BUILD/$name.log"; then
        echo "ЗБІРКА ПРОВАЛЕНА: $name" >"$BUILD/$name.out"; cat "$BUILD/$name.log" >>"$BUILD/$name.out"; echo 1 >"$BUILD/$name.rc"; return
    fi
    "$BUILD/$name" >"$BUILD/$name.res"; local trc=$?
    cp "$BUILD/$name.res" "$BUILD/$name.out"
    [ $trc -ne 0 ] && echo "ТЕСТ ПАДАЄ: $name" >>"$BUILD/$name.out"
    echo $trc >"$BUILD/$name.rc"
}
if [ $core_rc -ne 0 ]; then echo "Збірка ядер провалена"; exit 1; fi
tests=()
for t in "$DIR"/test_*.c; do [ -e "$t" ] && tests+=("$t"); done
for t in "${tests[@]}"; do run_one "$t" & while [ "$(jobs -rp | wc -l)" -ge 8 ]; do sleep 0.05; done; done
wait
total=0; passed=0; rc=0
for t in "${tests[@]}"; do
    name="$(basename "$t" .c)"
    out="$(cat "$BUILD/$name.out")"
    echo "$out" | grep -v '^TESTS ' || true
    [ "$(cat "$BUILD/$name.rc")" != 0 ] && rc=1
    read -r _ n p < <(echo "$out" | grep '^TESTS ' | tail -1)
    total=$((total + ${n:-0})); passed=$((passed + ${p:-0}))
done

echo "$total тестів, $passed пройдено"
[ "$total" -eq 0 ] && { echo "Жодного тесту не знайдено у $DIR"; exit 1; }
[ "$total" -ne "$passed" ] && rc=1
exit $rc
