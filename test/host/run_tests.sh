#!/usr/bin/env bash
# Хостовий раннер: збирає кожен test_*.c разом з усіма components/*/*_core.c звичайним gcc,
# запускає і підсумовує. Порожній набір (0 тестів) вважається помилкою.
# Використання: test/host/run_tests.sh [каталог_з_тестами]
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="${1:-$ROOT/test/host}"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

INCLUDES=()
for d in "$ROOT"/components/*/include; do [ -d "$d" ] && INCLUDES+=("-I$d"); done
INCLUDES+=("-I$ROOT/test/host")
mapfile -t CORES < <(ls "$ROOT"/components/*/*_core.c 2>/dev/null)

total=0; passed=0; rc=0
for t in "$DIR"/test_*.c; do
    [ -e "$t" ] || continue
    name="$(basename "$t" .c)"
    if ! gcc -std=c11 -Wall -Wextra -Werror "${INCLUDES[@]}" -o "$BUILD/$name" "$t" "${CORES[@]}" 2>"$BUILD/$name.log"; then
        echo "ЗБІРКА ПРОВАЛЕНА: $name"; cat "$BUILD/$name.log"; rc=1; continue
    fi
    out="$("$BUILD/$name")"; trc=$?
    echo "$out" | grep -v '^TESTS ' || true
    [ $trc -ne 0 ] && { echo "ТЕСТ ПАДАЄ: $name"; rc=1; }
    read -r _ n p < <(echo "$out" | grep '^TESTS ' | tail -1)
    total=$((total + ${n:-0})); passed=$((passed + ${p:-0}))
done

echo "$total тестів, $passed пройдено"
[ "$total" -eq 0 ] && { echo "Жодного тесту не знайдено у $DIR"; exit 1; }
[ "$total" -ne "$passed" ] && rc=1
exit $rc
