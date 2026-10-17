#!/usr/bin/env bash
# Перевірка AC-10: захищені файли не змінені без згоди Розробника (constitution, п. 10–11).
# Дивиться робоче дерево, індекс і вже закомічені зміни відносно бази (BASE, типово main).
# Згода: ALLOW_PROTECTED=1.
# Використання: test/host/check_protected.sh [корінь_репозиторію]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
[ "${ALLOW_PROTECTED:-0}" = 1 ] && { echo "check_protected: пропущено (ALLOW_PROTECTED=1)"; exit 0; }
PROTECTED='^(CMakeLists\.txt|sdkconfig\.defaults)$|^(\.vscode|kicad-rover)/'
BASE="${BASE:-main}"
cd "$ROOT" || exit 2
changed="$(git status --porcelain | awk '{print $NF}' | grep -E "$PROTECTED")"
if git rev-parse --verify -q "$BASE" >/dev/null; then
    committed="$(git diff --name-only "$BASE"...HEAD | grep -E "$PROTECTED")"
    changed="$(printf '%s\n%s\n' "$changed" "$committed" | sed '/^$/d' | sort -u)"
fi
if [ -n "$changed" ]; then
    echo "ЗМІНЕНО ЗАХИЩЕНІ ФАЙЛИ (потрібна явна згода Розробника):"; echo "$changed" | sed 's/^/    /'; exit 1
fi
echo "check_protected: чисто"
exit 0
