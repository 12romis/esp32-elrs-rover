#!/usr/bin/env bash
# Перевірка AC-10: захищені файли не змінені без згоди Розробника (constitution, п. 10–11).
# Дивиться робоче дерево і індекс. Згода: ALLOW_PROTECTED=1.
# Використання: test/host/check_protected.sh [корінь_репозиторію]
ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
[ "${ALLOW_PROTECTED:-0}" = 1 ] && { echo "check_protected: пропущено (ALLOW_PROTECTED=1)"; exit 0; }
changed="$(cd "$ROOT" && git status --porcelain | awk '{print $NF}' \
    | grep -E '^(CMakeLists\.txt|sdkconfig\.defaults)$|^(\.vscode|kicad-rover)/')"
if [ -n "$changed" ]; then
    echo "ЗМІНЕНО ЗАХИЩЕНІ ФАЙЛИ (потрібна явна згода Розробника):"; echo "$changed" | sed 's/^/    /'; exit 1
fi
echo "check_protected: чисто"
exit 0
