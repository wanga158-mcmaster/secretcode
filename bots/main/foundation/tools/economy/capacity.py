"""Per-map capacity summary (economy.md E13-E16, maps.md P4): merges the speedrun outputs into
data/economy/capacity.json and into each map record of data/maps.json ("capacity").

  python -I capacity.py

Binding class (from the items schedule's time split and its supply floor):
  respawn  - the schedule is within 35% of the supply floor (camps, not labour, are scarce);
  capture  - capture waits are >= 15% of champion time (many tiny groups);
  travel   - walking (to camps and altars) plus channels >= 55% of champion time;
  damage   - fighting plus recall trips >= 45% of champion time (health per gem binds);
  mixed    - none of the above.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
DATA = ROOT / "bots" / "main" / "foundation" / "data"


def main_table():
    out = {}
    p = ROOT / ".cache" / "main" / "analysis" / "economy" / "table.md"
    if p.exists():
        for line in p.read_text(encoding="utf-8").splitlines():
            m = re.match(r"\| (\S+) \| (\d) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \((\d+)/(\d+)\)", line)
            if m:
                out[m.group(1)] = {"main_floor": int(m.group(3)), "main_est_full": int(m.group(4)),
                                   "main_est_half": int(m.group(5)), "main_actual": int(m.group(6)),
                                   "main_actual_A": int(m.group(7)), "main_actual_B": int(m.group(8))}
    return out


def binding(r, sched):
    u = r["detail"]["use"]
    tot = sum(u.values()) or 1
    f = {k: v / tot for k, v in u.items()}
    cls = []
    if sched <= 1.35 * r["supply_floor"]:
        cls.append("respawn")
    if f.get("capture", 0) >= 0.15:
        cls.append("capture")
    if f.get("travel", 0) + f.get("bank", 0) >= 0.55:
        cls.append("travel")
    if f.get("fight", 0) + f.get("out", 0) >= 0.45:
        cls.append("damage")
    return cls or ["mixed"], {k: round(v, 3) for k, v in f.items() if v > 0}


def main() -> int:
    runs = {}
    for terr in ("full", "half"):
        for it in ("none", "spawn", "instant"):
            p = DATA / "economy" / f"speedrun_{terr}_{it}.json"
            if p.exists():
                runs[(terr, it)] = {r["map"]: r for r in json.loads(p.read_text(encoding="utf-8"))}
    mt = main_table()
    cap = {}
    for key, spawn in runs[("full", "spawn")].items():
        inst = runs[("full", "instant")][key]
        best = spawn if spawn["schedule"] <= inst["schedule"] else inst
        cls, split = binding(spawn, best["schedule"])
        half_s, half_i = runs[("half", "spawn")][key], runs[("half", "instant")][key]
        half = half_s if half_s["schedule"] <= half_i["schedule"] else half_i
        cap[key] = {"supply_floor": spawn["supply_floor"], "supply_floor_half": runs[("half", "spawn")][key]["supply_floor"],
                    "labour_bound_base": spawn["labour_bound"], "labour_rate_base": spawn["labour_rate"],
                    "schedule_base": runs[("full", "none")][key]["schedule"],
                    "schedule_items": best["schedule"], "schedule_items_half": half["schedule"],
                    "schedule_base_half": runs[("half", "none")][key]["schedule"],
                    "policy": best["policy"], "time_split": split, "binding": cls,
                    "bank_at_400": best["detail"]["bank_at"].get("400", best["detail"]["bank_at"].get(400)), "captures": best["detail"]["captures"],
                    "tributes": best["detail"]["tributes"], "gold": round(best["detail"]["gold"]),
                    **mt.get(key, {})}
    (DATA / "economy" / "capacity.json").write_text(json.dumps(cap, indent=1) + "\n", encoding="utf-8")
    atlas = json.loads((DATA / "maps.json").read_text(encoding="utf-8"))
    for m in atlas["maps"]:
        if m["key"] in cap:
            m["capacity"] = cap[m["key"]]
    (DATA / "maps.json").write_text(json.dumps(atlas, indent=1) + "\n", encoding="utf-8")
    print(f"capacity for {len(cap)} maps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
