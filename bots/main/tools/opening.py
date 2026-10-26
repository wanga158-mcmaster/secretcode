"""Opening economy (bots/main): when each side clears, earns and shops in the first rounds.

  python -B bots/main/tools/opening.py online [VERSION ...]     survey games, us vs them by rank tier
  python -B bots/main/tools/opening.py local DIR [--subject main] [--by-map]
                                                              local summaries (DIR/**/<game>.json)

Per team and game, from match summaries: round of the 1st and 5th camp clear, clears by r50 and
r100, earned gold at r50/100/200/300 (sum of champion earnedGold timelines), the first shard
bought, gold spent and emblems completed by r200/r300 (itemCombination). Prints medians per side. Online the
opponent's side shows what stronger teams do in the same games (D106: the top-20 gap is economy).
"""
from __future__ import annotations

import argparse
import gzip
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import online  # noqa: E402
import scout  # noqa: E402

TIERS = [(1, 20), (21, 60), (61, 200), (201, 99999)]
METRICS = ["clear1", "clear5", "clears100", "gold100", "gold200", "gold300", "spent200", "spent300", "buy1", "emb200", "emb300"]


def opening(s: dict, team: str) -> dict:
    ids = {c["id"] for c in s["roster"]["champions"] if c["team"] == team}
    clears, buys, embs = [], [], []
    for e in s["events"]:
        owner = e.get("owner") or {}
        if owner.get("id") not in ids or owner.get("kind") != "champion":
            continue
        rounds = [x["timestamp"]["round"] for x in e.get("entries", [])]
        if e["kind"] == "campClear":
            clears += rounds
        elif e["kind"] == "buyItem":
            buys += rounds
        elif e["kind"] == "itemCombination":
            embs += rounds
    clears.sort()
    gold, spent = defaultdict(float), defaultdict(float)
    for t in s["championTimelines"]:
        if t["owner"].get("id") in ids and t["metric"] in ("earnedGold", "spentGold"):
            for r in (50, 100, 200, 300):
                (gold if t["metric"] == "earnedGold" else spent)[r] += scout.value_at(t["runs"], r)
    return {"clear1": clears[0] if clears else 999, "clear5": clears[4] if len(clears) > 4 else 999,
            "clears50": sum(r <= 50 for r in clears), "clears100": sum(r <= 100 for r in clears),
            "gold50": gold[50], "gold100": gold[100], "gold200": gold[200], "gold300": gold[300],
            "spent200": spent[200], "spent300": spent[300],
            "buy1": min(buys) if buys else 999, "emb200": sum(r <= 200 for r in embs), "emb300": sum(r <= 300 for r in embs)}


def show(label: str, rows: list[dict]) -> None:
    if not rows:
        return
    cells = []
    for m in METRICS:
        v = statistics.median(r[m] for r in rows)
        cells.append(f"{v:6.0f}" if v >= 10 else f"{v:6.1f}")
    print(f"{label:24s} {len(rows):4d} " + " ".join(cells))


def header() -> None:
    print(f"{'side':24s} {'n':>4s} " + " ".join(f"{m:>6s}" for m in METRICS))


def cmd_online(versions: list[str]) -> int:
    reg = online.load()
    vs = versions or sorted({str(g["version"]) for g in reg if g.get("kind") == "survey"}, key=lambda v: int(v) if v.isdigit() else 0)
    for ver in vs:
        by = defaultdict(lambda: ([], []))
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
                if not s:
                    continue
                us, them = ("teamA", "teamB") if ours["side"] == "a" else ("teamB", "teamA")
                tier = next(t for t in TIERS if t[0] <= rank <= t[1])
                size = "small" if m["map"].startswith("00") else "large"
                by[(tier, size)][0].append(opening(s, us))
                by[(tier, size)][1].append(opening(s, them))
        if not by:
            continue
        print(f"v{ver}")
        header()
        for (tier, size), (u, t) in sorted(by.items()):
            name = f"{tier[0]}-{tier[1] if tier[1] < 99999 else ''} {size}"
            show(f"  us   {name}", u)
            show(f"  them {name}", t)
    return 0


def cmd_local(d: Path, subject: str, by_map: bool) -> int:
    rows = defaultdict(lambda: ([], []))
    for f in sorted(d.rglob("*.json")):
        parts = f.stem.split("~")[0].split("_")
        if len(parts) < 4 or f.name in ("state.json",):
            continue
        try:
            s = json.loads(f.read_text(encoding="utf-8")) if f.suffix == ".json" else json.loads(gzip.decompress(f.read_bytes()))
        except (OSError, ValueError):
            continue
        if "roster" not in s:
            continue
        a, b = parts[-3], parts[-2]
        if subject not in (a, b):
            continue
        us, them = ("teamA", "teamB") if a == subject else ("teamB", "teamA")
        key = "_".join(parts[:-3]) if by_map else "all"
        rows[key][0].append(opening(s, us))
        rows[key][1].append(opening(s, them))
    header()
    for k, (u, t) in sorted(rows.items()):
        show(f"  {subject[:10]} {k}", u)
        show(f"  other {k}", t)
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("mode", choices=["online", "local"])
    p.add_argument("args", nargs="*")
    p.add_argument("--subject", default="main")
    p.add_argument("--by-map", action="store_true")
    a = p.parse_args()
    if a.mode == "online":
        return cmd_online(a.args)
    return cmd_local(Path(a.args[0]), a.subject, a.by_map)


if __name__ == "__main__":
    sys.exit(main())
