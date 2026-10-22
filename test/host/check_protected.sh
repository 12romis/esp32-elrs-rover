#!/usr/bin/env bash
# Перевірка AC-10: захищені файли не змінені без згоди Розробника (constitution, п. 10–11).
# Дивиться робоче дерево, індекс і вже закомічені зміни відносно бази (BASE, типово main).
# Згода: ALLOW_PROTECTED=1 дозволяє лише правки `CMakeLists.txt` кореня та `sdkconfig.defaults` (phase8-session-report, spec A22;
# кожну записано в docs/features/<фаза>/protected-changes.md) і друкує список змінених; `.vscode/` і `kicad-rover/` не
# дозволяються жодною згодою цього скрипта (п. 10 для .vscode, п. 11 для kicad-rover).
# Використання: test/host/check_protected.sh [корінь_репозиторію]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
PROTECTED='^(CMakeLists\.txt|sdkconfig\.defaults)$|^(\.vscode|kicad-rover)/'
NEVER='^(\.vscode|kicad-rover)/'
BASE="${BASE:-main}"
cd "$ROOT" || exit 2
changed="$(git status --porcelain | awk '{print $NF}' | grep -E "$PROTECTED")"
if git rev-parse --verify -q "$BASE" >/dev/null; then
    committed="$(git diff --name-only "$BASE"...HEAD | grep -E "$PROTECTED")"
    changed="$(printf '%s\n%s\n' "$changed" "$committed" | sed '/^$/d' | sort -u)"
fi
if [ "${ALLOW_PROTECTED:-0}" = 1 ]; then
    hard="$(printf '%s\n' "$changed" | grep -E "$NEVER")"
    if [ -n "$hard" ]; then
        echo "ЗМІНЕНО ФАЙЛИ, ЯКІ НЕ МОЖНА ДОЗВОЛИТИ (.vscode/, kicad-rover/):"; echo "$hard" | sed 's/^/    /'; exit 1
    fi
    if [ -n "$changed" ]; then
        echo "check_protected: дозволено ALLOW_PROTECTED=1, змінені захищені файли (мають бути в protected-changes.md):"
        echo "$changed" | sed 's/^/    /'
    else
        echo "check_protected: чисто"
    fi
    exit 0
fi
if [ -n "$changed" ]; then
    echo "ЗМІНЕНО ЗАХИЩЕНІ ФАЙЛИ (потрібна явна згода Розробника):"; echo "$changed" | sed 's/^/    /'; exit 1
fi
echo "check_protected: чисто"
exit 0
