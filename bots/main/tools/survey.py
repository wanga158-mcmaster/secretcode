"""Survey summary (bots/main): our results against the ecosystem per version, from the keeper's groups.

  python -B bots/main/tools/survey.py [VERSION ...]
  python -B bots/main/tools/survey.py --compare A B

Reads results/online/groups.json entries of kind "survey" (tools/online.py keep: one random opponent
per group drawn by rank tier, five random maps and sides) and prints per version the record and, per
rank tier and map size, wins, our deaths and kills per game (from the gzipped summaries). Opponents and
maps are random, so versions compare as populations, not paired games: read the tier rows with their
sample sizes (a tier row of 30 games has a standard error of about 9 points). --compare prints, per
tier and size, B - A for the win rate, deaths, kills, earned gold at r300 and non-kill gold per 1K
monster damage at r300, each with its standard error.
"""
from __future__ import annotations

import argparse
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import online  # noqa: E402
import scout  # noqa: E402

TIERS = [(1, 20), (21, 60), (61, 200), (201, 99999)]


def gold_stats(s: dict, team: str) -> dict:
    ids = {c["id"] for c in s["roster"]["champions"] if c["team"] == team}
    earned = kills = mdmg = 0.0
    for t in s["championTimelines"]:
        if t["owner"].get("id") not in ids:
            continue
        if t["metric"] == "earnedGold":
            earned += scout.value_at(t["runs"], 300)
        elif t["metric"] == "kills":
            kills += scout.value_at(t["runs"], 300)
        elif t["metric"] == "monsterDamageDone":
            mdmg += scout.value_at(t["runs"], 300)
    return {"gold300": earned, "conv300": (earned - 100.0 * kills) / max(1.0, mdmg / 1000.0)}


def collect(reg: list, ver: str) -> list[tuple]:
    rows = []
    for g in reg:
        if g.get("kind") != "survey" or str(g["version"]) != ver or g["state"] != "done":
            continue
        rank = (g.get("created") or {}).get("rank", 0)
        gdir = online.ONLINE / str(g["version"]) / g["group"]
        for m in (g.get("show") or {}).get("matches", []):
            ours = next((t for t in m["teams"] if t["teamId"] == online.OUR_TEAM), None)
            if not ours or ours.get("result") not in ("win", "loss"):
                continue
            s = scout.load_summary(gdir, m["matchId"])
            team = "teamA" if ours["side"] == "a" else "teamB"
            st = scout.team_stats(s, team) if s else None
            if st is not None:
                st.update(gold_stats(s, team))
            rows.append((rank, m["map"].startswith("00"), ours["result"] == "win", st))
    return rows


def mean_se(xs: list[float]) -> tuple[float, float]:
    if not xs:
        return float("nan"), float("nan")
    m = statistics.fmean(xs)
    return m, (statistics.stdev(xs) / math.sqrt(len(xs)) if len(xs) > 1 else float("nan"))


def compare(reg: list, a: str, b: str) -> int:
    ra, rb = collect(reg, a), collect(reg, b)
    print(f"B - A with standard errors (A = v{a}, {len(ra)} games; B = v{b}, {len(rb)} games)")
    print(f"{'tier':>12s} {'size':5s} {'nA':>4s} {'nB':>4s} {'win% B-A':>14s} {'deaths':>13s} {'kills':>13s} {'gold r300':>15s} {'gold/1K dmg':>13s}")
    for lo, hi in TIERS + [(1, 99999)]:
        for small in (True, False):
            sa = [r for r in ra if lo <= r[0] <= hi and r[1] == small]
            sb = [r for r in rb if lo <= r[0] <= hi and r[1] == small]
            if not sa or not sb:
                continue
            cells = []
            for key in ("win", "deaths", "kills", "gold300", "conv300"):
                if key == "win":
                    xa, xb = [100.0 * r[2] for r in sa], [100.0 * r[2] for r in sb]
                else:
                    xa = [r[3][key] for r in sa if r[3]]
                    xb = [r[3][key] for r in sb if r[3]]
                (ma, ea), (mb, eb) = mean_se(xa), mean_se(xb)
                cells.append(f"{mb - ma:+7.1f} +-{math.hypot(ea, eb):5.1f}")
            name = f"{lo}-{hi if hi < 99999 else ''}" if (lo, hi) != (1, 99999) else "all"
            print(f"{name:>12s} {'small' if small else 'large':5s} {len(sa):4d} {len(sb):4d} " + " ".join(cells))
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("versions", nargs="*")
    p.add_argument("--compare", nargs=2, metavar=("A", "B"))
    a = p.parse_args()
    reg = online.load()
    if a.compare:
        return compare(reg, *a.compare)
    versions = a.versions or sorted({g["version"] for g in reg if g.get("kind") == "survey"}, key=lambda v: int(v) if v.isdigit() else 0)
    for ver in versions:
        rows = collect(reg, str(ver))
        if not rows:
            continue
        print(f"v{ver}: {sum(r[2] for r in rows)}/{len(rows)} wins")
        for lo, hi in TIERS:
            for small in (True, False):
                sel = [r for r in rows if lo <= r[0] <= hi and r[1] == small]
                if not sel:
                    continue
                st = [r[3] for r in sel if r[3]]
                d = sum(x["deaths"] for x in st) / len(st) if st else float("nan")
                k = sum(x["kills"] for x in st) / len(st) if st else float("nan")
                print(f"   rank {lo:3d}-{hi if hi < 99999 else '':<5} {'small' if small else 'large'}: {sum(r[2] for r in sel):3d}/{len(sel):<3d} "
                      f"({100 * sum(r[2] for r in sel) / len(sel):3.0f}%)  deaths {d:4.1f}  kills {k:4.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
