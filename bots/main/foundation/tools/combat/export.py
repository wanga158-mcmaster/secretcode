"""Regenerate foundation/data/combat/*.json from the duel.py runs in .cache/contenders/combat/runs/.

  python export.py [RUNS_DIR]

Every table is engine evidence (one game per scenario key) except model_*.json, which compare the
turn-exact model (duelsim.py) with those games. Scenario sets: the s_*.json files next to RUNS_DIR
(regenerate with the generator lines recorded in combat.md, "How to rerun").
"""
from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyze  # noqa: E402
import duel  # noqa: E402
import duelsim  # noqa: E402

ROOT = HERE.parents[4]
OUT = ROOT / "bots/main/foundation/data/combat"
NAMES = analyze.NAMES


def margins(run: Path, pol_of_x=None) -> dict:
    """Margin of pick x against pick y from x's side: survivor health share if x won, minus the
    winner's share if x lost, 0 for no death; mean over both sides and parities."""
    cell = defaultdict(list)
    for r in duel.parse(run):
        f = analyze.game_facts(r)
        if f is None:
            continue
        s = r["scenario"]
        a, b = s["pick"]
        pa, pb = s.get("pol", [0, 0])
        for x, y, side, px in ((a, b, "A", pa), (b, a, "B", pb)):
            if pol_of_x is not None and px != pol_of_x:
                continue
            m = 0.0 if f["winner"] is None else (f["surv_frac"] if f["winner"] == side else -f["surv_frac"])
            cell[f"{x},{y}"].append(round(m, 3))
    return {k: {"mean": round(sum(v) / len(v), 3), "games": v} for k, v in cell.items()}


def table(m: dict) -> list[str]:
    rows = ["X \\ Y " + " ".join(n[:6] for n in NAMES)]
    for x in range(12):
        rows.append(NAMES[x][:10] + " " + " ".join(
            (f"{m[f'{x},{y}']['mean']:+.2f}" if f"{x},{y}" in m else "  .  ") for y in range(12)))
    return rows


def main(argv: list[str]) -> int:
    runs = Path(argv[0]).resolve() if argv else ROOT / ".cache/contenders/combat/runs"
    OUT.mkdir(parents=True, exist_ok=True)
    meta = {"engine": duel.games.ENGINE, "names": NAMES,
            "margin": "survivor health share of the winner, signed from the row pick's side; mean of both sides x both parities"}
    out = {}
    out["duels_base"] = {**meta, "build": "base stats", "policies": "FIGHT both", "cells": margins(runs / "base")}
    out["duels_b11"] = {**meta, "build": "H10 S6 F1 each (450 gold; Cassi & Issac split it)", "cells": margins(runs / "b11")}
    out["duels_online"] = {**meta, "build": "Heart F11 H5 Durability; others H10 S6 F1", "cells": margins(runs / "metab")}
    out["kite"] = {**meta, "policies": "row pick KITE (ranged steps away at distance <= 2, towards a neutral corner), column FIGHT",
                   "cells": margins(runs / "kite2", pol_of_x=1)}
    out["standoff"] = {**meta, "policies": "row pick STANDOFF (hold outside the enemy's two-turn strike reach), column FIGHT",
                       "cells": margins(runs / "standoff3", pol_of_x=12)}
    # equal-gold stat duels: same champion, builds a vs b
    stat = defaultdict(list)
    for r in duel.parse(runs / "stat"):
        f = analyze.game_facts(r)
        if f is None:
            continue
        s = r["scenario"]
        ba, bb = s["build"]
        m = 0.0 if f["winner"] is None else (f["surv_frac"] if f["winner"] == "A" else -f["surv_frac"])
        stat[f"{s['pick'][0]}:{ba}-{bb}"].append(round(m, 3))
        stat[f"{s['pick'][0]}:{bb}-{ba}"].append(round(-m, 3))
    out["stat_duels"] = {**meta, "builds": {str(k): v for k, v in duelsim.BUILDS.items()},
                         "cells": {k: {"mean": round(sum(v) / len(v), 3), "games": v} for k, v in sorted(stat.items())}}
    online = defaultdict(list)
    for r in duel.parse(runs / "online"):
        f = analyze.game_facts(r)
        if f is None:
            continue
        s = r["scenario"]
        m = 0.0 if f["winner"] is None else (f["surv_frac"] if f["winner"] == "A" else -f["surv_frac"])
        if s["build"][0] == 15:
            m = -m
        online[str(s["pick"][0])].append(round(m, 3))
    out["builds_health_vs_strength"] = {**meta, "builds": "health-first (11; Heart 12) against strength-first (15), same champion",
                                        "cells": {k: {"mean": round(sum(v) / len(v), 3), "games": v} for k, v in online.items()}}
    slot_chase = [f for f in (analyze.chase_facts(r) for r in duel.parse(runs / "slot") if r["scenario"].get("place") == 7) if f]
    out["chase"] = {**meta, "slots": "slot_shift false: fleer and chaser are both first picks (same turn slot); true: team B's duelist acts in slot 3",
                    "rows": analyze.chase_table(runs / "chase") + analyze.chase_table(runs / "fleehit") + slot_chase}
    mir = []
    for r in duel.parse(runs / "slot"):
        s = r["scenario"]
        if s.get("place") != 4:
            continue
        f = analyze.game_facts(r)
        mir.append({"pick": s["pick"][0], "parity": s["parity"], "winner": f["winner"], "surv_frac": f["surv_frac"],
                    "first_side": f["first_side"]})
    out["slot_mirrors"] = {**meta, "setup": "mirror duels, A's duelist in turn slot 1, B's in slot 3 (B fillers priority 5)", "rows": mir}
    groups = []
    for run in ("g1", "stun", "camp"):
        for r in duel.parse(runs / run):
            o = r.get("outcome") or {}
            stunned = {}
            for t in (r.get("summary") or {}).get("turns", []):
                if t["stunned"]:
                    stunned.setdefault(f"{t['t']}:{t['c']}", []).append(int(t["r"]) - o.get("start", 0))
            groups.append({"run": run, "scenario": r["scenario"], "winner": o.get("winner"), "alive": o.get("alive"),
                           "hp_frac": o.get("hp_frac"), "deaths": o.get("deaths"), "stunned_turns": stunned,
                           "errors": o.get("errors")})
    out["groups"] = {**meta, "table_rows": "bots/contenders/_duel-lab/src/table.h", "rows": groups}
    cal = {}
    for run in ("base", "b11", "stat", "online", "metab"):
        c = duelsim.compare(runs / run)
        cal[run] = {k: c[k] for k in ("games", "winner_agree", "hp_exact", "hp_err_mean")}
        cal[run]["single_body_diffs"] = [row for row in c["rows"] if 3 not in row["pick"] and
                                         (row["engine"] != row["model"] or abs(row["engine_hp"] - row["model_hp"]) > 0.5)]
    out["model_calibration"] = {**meta, "runs": cal}
    for name, data in out.items():
        (OUT / f"{name}.json").write_text(json.dumps(data, indent=0, sort_keys=True), encoding="utf-8")
    for name in ("duels_base", "duels_b11", "duels_online", "kite", "standoff"):
        print(f"== {name}")
        for line in table(out[name]["cells"]):
            print(line)
    print("calibration:", json.dumps({k: {kk: vv for kk, vv in v.items() if kk != "single_body_diffs"} for k, v in cal.items()}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
