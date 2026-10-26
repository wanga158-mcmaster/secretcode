"""Markdown tables for maps.md and economy.md from the data files (no recomputation).

  python -I tables_md.py atlas|speedrun|clear|items [--set pool|legacy|all]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
DATA = ROOT / "bots" / "main" / "foundation" / "data"


def main_table():
    out = {}
    p = ROOT / ".cache" / "main" / "analysis" / "economy" / "table.md"
    if not p.exists():
        return out
    for line in p.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\| (\S+) \| (\d) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \((\d+)/(\d+)\)", line)
        if m:
            out[m.group(1)] = {"floor": int(m.group(3)), "est_full": int(m.group(4)), "est_half": int(m.group(5)),
                               "ours": int(m.group(6)), "A": int(m.group(7)), "B": int(m.group(8))}
    return out


def atlas(which):
    d = json.loads((DATA / "maps.json").read_text(encoding="utf-8"))
    print("| map | size | draft | spawn walk | altars (nearest) | groups own/cont/theirs | gem cap /rd | harmless gems /rd | big-gem share | mid width | classes |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for m in d["maps"]:
        if which != "all" and m["set"] != which:
            continue
        g = m["groups"]
        own = sum(1 for x in g if x["owner"] == "A")
        con = sum(1 for x in g if x["owner"] == "contested")
        th = sum(1 for x in g if x["owner"] == "B")
        alt = [a["dA"] for a in m["altars"] if a["dA"] is not None]
        mm, t = m["metrics"], m["terrain"]
        width = t["mid_paths"] if t["mid_paths"] is not None else t["disjoint_paths"]
        s2s = m["spawn_to_spawn"] if m["spawn_to_spawn"] is not None else f"gated ({m['spawn_to_spawn_open']})"
        print(f"| {m['key']} | {m['width']}x{m['height']} | {m['draft']} | {s2s} | {len(m['altars'])} ({min(alt) if alt else '-'}) | "
              f"{own}/{con}/{th} | {mm['gem_rate_cap']:.2f} | {mm['harmless_gem_rate']:.2f} | {mm['gem_share_big']:.2f} | "
              f"{width}{'+' if width == 12 else ''} | {', '.join(m['classes']) or '-'} |")


def speedrun(which):
    mt = main_table()
    runs = {}
    for terr in ("full", "half"):
        for it in ("none", "spawn", "instant"):
            p = DATA / "economy" / f"speedrun_{terr}_{it}.json"
            if p.exists():
                runs[(terr, it)] = {r["map"]: r for r in json.loads(p.read_text(encoding="utf-8"))}
    print("| map | floor | base | items | half | main est | main actual | binds (items run: largest shares) |")
    print("|---|---|---|---|---|---|---|---|")
    for key, r in runs[("full", "spawn")].items():
        if which == "pool" and key.startswith("legacy/"):
            continue
        if which == "legacy" and not key.startswith("legacy/"):
            continue
        base = runs[("full", "none")][key]["schedule"]
        items = min(r["schedule"], runs[("full", "instant")][key]["schedule"])
        half = min(runs.get(("half", "spawn"), {}).get(key, {"schedule": 2000})["schedule"],
                   runs.get(("half", "instant"), {}).get(key, {"schedule": 2000})["schedule"])
        u = r["detail"]["use"]
        tot = sum(u.values()) or 1
        top = sorted(((v / tot, k) for k, v in u.items()), reverse=True)[:2]
        m = mt.get(key, {})
        print(f"| {key} | {r['supply_floor']} | {base} | {items} | {half} | {m.get('est_full', '-')} | {m.get('ours', '-')} | "
              + ", ".join(f"{k} {100 * f:.0f}%" for f, k in top) + " |")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("what")
    p.add_argument("--set", default="pool")
    a = p.parse_args()
    {"atlas": atlas, "speedrun": speedrun}[a.what](a.set)
    return 0


if __name__ == "__main__":
    sys.exit(main())
