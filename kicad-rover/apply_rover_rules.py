#!/usr/bin/env python3
"""
Дописує у файл проєкту KiCad (.kicad_pro) налаштування DRC, набори
доріжок/перехідних і net class для плати ровера ESP32-ELRS.

Навмисно НЕ перезаписує файл цілком: змінює тільки потрібні ключі,
решту (зокрема все, що зʼявилось у нових версіях KiCad) лишає як є.
Нові net class створюються копіюванням наявного класу Default, тому
структура полів завжди відповідає твоїй версії KiCad.

ВАЖЛИВО: KiCad має бути ЗАКРИТИЙ, інакше він перезапише файл при виході.

Використання:
    python3 apply_rover_rules.py /шлях/до/kicad-rover.kicad_pro
    python3 apply_rover_rules.py /шлях/до/kicad-rover.kicad_pro --dry-run
"""

import copy
import json
import shutil
import sys
from pathlib import Path

# ── DRC: мінімальні обмеження ────────────────────────────────────────────
# Значення з запасом до можливостей JLCPCB (вони тягнуть 0.127 мм),
# але саме такі лишаються в безкоштовному тарифі й прощають похибки.
RULES = {
    "min_clearance": 0.2,
    "min_track_width": 0.2,
    "min_via_diameter": 0.6,
    "min_via_annular_width": 0.13,
    "min_through_hole_diameter": 0.3,
    "min_hole_to_hole": 0.5,
    "min_hole_clearance": 0.25,
    "min_copper_edge_clearance": 0.3,
    "min_silk_clearance": 0.15,
    "min_text_height": 0.8,
    "min_text_thickness": 0.15,
    "min_resolved_spokes": 2,
}

# ── Готові розміри доріжок і перехідних (перший 0.0 = «з net class») ──────
TRACK_WIDTHS = [0.0, 0.25, 0.8, 1.5, 2.5]
VIA_DIMENSIONS = [
    {"diameter": 0.0, "drill": 0.0},
    {"diameter": 0.6, "drill": 0.3},
    {"diameter": 0.8, "drill": 0.4},
]

# ── Net class: тільки поля, які треба перевизначити ───────────────────────
NETCLASSES = {
    "Default":  {"clearance": 0.2, "track_width": 0.25,
                 "via_diameter": 0.6, "via_drill": 0.3},
    "Power":    {"clearance": 0.2, "track_width": 0.8,
                 "via_diameter": 0.8, "via_drill": 0.4},
    "Battery":  {"clearance": 0.3, "track_width": 2.5,
                 "via_diameter": 0.8, "via_drill": 0.4},
    "Motor":    {"clearance": 0.3, "track_width": 1.5,
                 "via_diameter": 0.8, "via_drill": 0.4},
}

# ── Привʼязка нетів до класів ─────────────────────────────────────────────
# Увага: /MOT_* спрацює лише після того, як ти даси моторним нетам мітки
# у схемі (MOT_FL_P, MOT_FL_N, ...). Зараз вони звуться Net-(M1-M+) тощо.
PATTERNS = [
    ("Power",   "+5V"),
    ("Power",   "+3V3_MCU"),
    ("Power",   "+3V3_PERIPH"),
    ("Battery", "+VBAT"),
    ("Motor",   "/MOT_*"),
]


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry_run = "--dry-run" in sys.argv

    if not args:
        print(__doc__)
        return 2

    path = Path(args[0]).expanduser()
    if not path.is_file():
        print(f"Не знайдено файл: {path}")
        return 1
    if path.suffix != ".kicad_pro":
        print(f"Очікувався .kicad_pro, а не {path.suffix}")
        return 1

    pro = json.loads(path.read_text(encoding="utf-8"))
    changes = []

    # ── DRC ───────────────────────────────────────────────────────────────
    ds = pro.setdefault("board", {}).setdefault("design_settings", {})
    rules = ds.setdefault("rules", {})
    for key, value in RULES.items():
        if rules.get(key) != value:
            changes.append(f"  rules.{key}: {rules.get(key)!r} -> {value!r}")
            rules[key] = value

    if ds.get("track_widths") != TRACK_WIDTHS:
        changes.append(f"  track_widths -> {TRACK_WIDTHS}")
        ds["track_widths"] = list(TRACK_WIDTHS)

    if ds.get("via_dimensions") != VIA_DIMENSIONS:
        changes.append(f"  via_dimensions -> {len(VIA_DIMENSIONS)} записи")
        ds["via_dimensions"] = copy.deepcopy(VIA_DIMENSIONS)

    # ── Net class ─────────────────────────────────────────────────────────
    ns = pro.setdefault("net_settings", {})
    classes = ns.setdefault("classes", [])

    # Шаблон беремо з наявного Default — так усі поля точно відповідають
    # структурі саме твоєї версії KiCad.
    template = next((c for c in classes if c.get("name") == "Default"), None)
    if template is None:
        if not classes:
            print("У .kicad_pro немає жодного net class, зокрема Default.\n"
                  "Відкрий Board Setup -> Net Classes, натисни OK, збережи "
                  "проєкт — і запусти скрипт ще раз.")
            return 1
        template = classes[0]

    by_name = {c.get("name"): c for c in classes}
    for name, overrides in NETCLASSES.items():
        target = by_name.get(name)
        if target is None:
            target = copy.deepcopy(template)
            target["name"] = name
            classes.append(target)
            changes.append(f"  net class '{name}': створено")
        for key, value in overrides.items():
            if target.get(key) != value:
                changes.append(f"  net class '{name}'.{key}: "
                               f"{target.get(key)!r} -> {value!r}")
                target[key] = value

    # ── Шаблони привʼязки ─────────────────────────────────────────────────
    existing = ns.get("netclass_patterns") or []
    known = {(p.get("netclass"), p.get("pattern")) for p in existing}
    for netclass, pattern in PATTERNS:
        if (netclass, pattern) not in known:
            existing.append({"netclass": netclass, "pattern": pattern})
            changes.append(f"  шаблон: {pattern} -> {netclass}")
    ns["netclass_patterns"] = existing

    # ── Запис ─────────────────────────────────────────────────────────────
    if not changes:
        print("Усе вже налаштовано, змінювати нічого.")
        return 0

    print(f"Зміни у {path.name}:")
    print("\n".join(changes))

    if dry_run:
        print("\n--dry-run: файл не змінено.")
        return 0

    backup = path.with_suffix(path.suffix + ".bak")
    shutil.copy2(path, backup)
    path.write_text(json.dumps(pro, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")

    print(f"\nЗаписано. Резервна копія: {backup.name}")
    print("Тепер відкрий проєкт і перевір Board Setup -> Design Rules "
          "та Net Classes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
