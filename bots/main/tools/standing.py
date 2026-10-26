"""Where we stand online (bots/main): win rate by version and opponent rank band from the registry, plus
our recent ranked games. Read-only; ranked rows need the server (cambc matches list, ladder).

  python -B bots/main/tools/standing.py [--versions 44,45] [--ranked N]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REGISTRY = ROOT / "results" / "online" / "groups.json"
CAMBC = ROOT / ".tools" / "cambc.exe"
ME = "34b36bab-66bb-4506-b032-2df6a081758d"
BANDS = [(1, 10), (11, 30), (31, 60), (61, 150), (151, 10000)]


def band(rank: int | None) -> str:
    if not rank:
        return "?"
    for lo, hi in BANDS:
        if lo <= rank <= hi:
            return f"{lo}-{hi if hi < 10000 else '+'}"
    return "?"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--versions", default="")
    p.add_argument("--ranked", type=int, default=0, help="also tabulate our last N ranked matches")
    a = p.parse_args()
    reg = json.loads(REGISTRY.read_text(encoding="utf-8"))
    want = set(a.versions.split(",")) if a.versions else None
    table = defaultdict(lambda: [0, 0])
    for g in reg:
        v = str(g.get("version"))
        if want and v not in want:
            continue
        b = band((g.get("created") or {}).get("rank"))
        for r in g.get("results", []) or []:
            parts = r.split(":")
            if len(parts) < 3:
                continue
            table[(v, b)][0] += parts[2] == "win"
            table[(v, b)][1] += 1
    versions = sorted({v for v, _ in table}, key=lambda x: (len(x), x))
    bands = [band(lo) for lo, _ in BANDS] + ["?"]
    print("version " + " ".join(f"{b:>13s}" for b in bands))
    for v in versions:
        cells = []
        for b in bands:
            w, n = table.get((v, b), (0, 0))
            cells.append(f"{w:4d}/{n:<4d}{100 * w / n:3.0f}%" if n else f"{'-':>13s}")
        print(f"{v:7s} " + " ".join(cells))
    if a.ranked:
        rank = {}
        for page in range(1, 9):
            out = subprocess.run([str(CAMBC), "ladder", "--page", str(page), "--limit", "100", "--json"], capture_output=True,
                                 text=True, encoding="utf-8").stdout
            try:
                teams = json.loads(out).get("teams", [])
            except ValueError:
                break
            if not teams:
                break
            rank.update({t["teamId"]: t["rank"] for t in teams})
        out = subprocess.run([str(CAMBC), "matches", "list", "--team", "me", "--kind", "ranked", "--limit", str(a.ranked), "--json"],
                             capture_output=True, text=True, encoding="utf-8").stdout
        rows = json.loads(out)
        rows = rows.get("matches", rows) if isinstance(rows, dict) else rows
        rt = defaultdict(lambda: [0, 0])
        for m in rows:
            teams = m.get("teams", [])
            mine = [t for t in teams if t.get("teamId") == ME]
            opp = [t for t in teams if t.get("teamId") != ME]
            if not mine or not opp or mine[0].get("result") not in ("win", "loss", "draw"):
                continue
            b = band(rank.get(opp[0]["teamId"]))
            rt[b][0] += mine[0]["result"] == "win"
            rt[b][1] += 1
        print("ranked (last %d, opponent's current rank): " % a.ranked +
              "  ".join(f"{b} {w}/{n}" for b, (w, n) in sorted(rt.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
