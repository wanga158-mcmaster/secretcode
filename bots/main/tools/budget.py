"""Time budget of online panel games (bots/main): where our champions' rounds go vs the opponent's.

  python -B bots/main/tools/budget.py VERSION [VERSION ...] [--rounds 400] [--teams NAME,NAME] [--panel FILE]

For every finished group of each version (results/online/groups.json) it reads each match's summary
and replay (tracks cached in .cache/main/tracks/<match-id>.json via tools/inspect-strategy-replay.mjs)
and counts champion-rounds 2..R per side:
  dead   champion dead            farm   its monster damage rose this round
  near   alive with an enemy champion within 6 (Chebyshev)     nfarm  near and farming
  fight  its champion damage rose this round                   gold / kills / deaths by round R
Prints per-version means for us and the opponents (small 00* and large 01* maps separately).
"""
from __future__ import annotations

import argparse
import io
import json
import subprocess
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import scout  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
ONLINE = ROOT / "results" / "online"
TRACKS = ROOT / ".cache" / "main" / "tracks"
NODE = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"
OUR_TEAM = "34b36bab-66bb-4506-b032-2df6a081758d"


def series(runs: list, n: int) -> list[float]:
    out, t = [0.0] * n, 0
    for r in runs:
        v = float(r["value"])
        for i in range(t, min(n, t + r["duration"])):
            out[i] = v
        t += r["duration"]
    for i in range(min(t, n), n):
        out[i] = out[i - 1] if i else 0.0
    return out


def tracks(gdir: Path, match_id: str) -> dict | None:
    TRACKS.mkdir(parents=True, exist_ok=True)
    path = TRACKS / f"{match_id}.json"
    if not path.exists():
        with zipfile.ZipFile(gdir / "group.zip") as z:
            name = next((n for n in z.namelist() if match_id in n), None)
            if not name:
                return None
            tmp = TRACKS / f"{match_id}.zip"
            tmp.write_bytes(z.read(name))
        out = subprocess.run([str(NODE), str(ROOT / "tools" / "inspect-strategy-replay.mjs"), "--replay", str(tmp), "--actor", "all"],
                             cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        tmp.unlink(missing_ok=True)
        if out.returncode:
            return None
        path.write_text(out.stdout, encoding="utf-8")
    return json.loads(path.read_text(encoding="utf-8"))


def side_budget(s: dict, t: dict, team: str, rounds: int) -> dict:
    team_idx = 0 if team == "teamA" else 1
    ids = [c["id"] for c in s["roster"]["champions"] if c["team"] == team]
    n = rounds + 2
    tl = {(x["owner"]["id"], x["metric"]): series(x["runs"], n) for x in s["championTimelines"] if x["owner"].get("kind") == "champion"}
    pos = {c["id"]: {smp[0]: (smp[1], smp[2]) for smp in c["samples"]} for c in t["tracks"]}
    team_of = {c["id"]: c["team"] for c in t["tracks"]}
    enemies = [cid for cid, tm in team_of.items() if tm != team_idx]
    b = defaultdict(float)
    for cid in ids:
        md, cd = tl.get((cid, "monsterDamageDone")), tl.get((cid, "championDamageDone"))
        for r in range(2, rounds + 1):
            p = pos.get(cid, {}).get(r)
            if not p or p[0] < 0:
                b["dead"] += 1
                continue
            farm = md is not None and md[r] > md[r - 1]
            near = any((q := pos.get(e, {}).get(r)) and q[0] >= 0 and max(abs(q[0] - p[0]), abs(q[1] - p[1])) <= 6 for e in enemies)
            b["farm"] += farm
            b["near"] += near
            b["nfarm"] += near and farm
            b["fight"] += cd is not None and cd[r] > cd[r - 1]
        for m in ("earnedGold", "kills", "deaths"):
            v = tl.get((cid, m))
            b[m] += v[rounds] if v else 0.0
    return b


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("versions", nargs="+")
    p.add_argument("--rounds", type=int, default=400)
    p.add_argument("--teams", default="", help="comma-separated opponent names to keep")
    p.add_argument("--panel", help="keep only groups of this panel file (same opponent and maps)")
    a = p.parse_args()
    keep = {x for x in a.teams.split(",") if x}
    panel = {(e["opponent"], ",".join(e["maps"])) for e in json.loads(Path(a.panel).read_text(encoding="utf-8"))} if a.panel else None
    reg = json.loads((ONLINE / "groups.json").read_text(encoding="utf-8"))
    cols = ["dead", "farm", "near", "nfarm", "fight", "earnedGold", "kills", "deaths"]
    print(f"{'version':8s} {'maps':6s} {'n':>3s} | " + " ".join(f"{c[:6]:>6s}" for c in cols) + " | opponent " + " ".join(f"{c[:6]:>6s}" for c in cols))
    for ver in a.versions:
        agg = defaultdict(lambda: [defaultdict(float), defaultdict(float), 0])
        for g in reg:
            name = (g.get("created") or {}).get("teamName", "")
            if str(g["version"]) != ver or g["state"] != "done" or (keep and name not in keep) or                     (panel is not None and (g["opponent"], g["maps"]) not in panel):
                continue
            gdir = ONLINE / ver / g["group"]
            for m in (g.get("show") or {}).get("matches", []):
                ours = next((x for x in m["teams"] if x["teamId"] == OUR_TEAM), None)
                s = scout.load_summary(gdir, m["matchId"]) if ours else None
                if not s:
                    continue
                t = tracks(gdir, m["matchId"])
                if not t:
                    continue
                us, them = ("teamA", "teamB") if ours["side"] == "a" else ("teamB", "teamA")
                size = "small" if m["map"].startswith("00") else "large"
                x, y = side_budget(s, t, us, a.rounds), side_budget(s, t, them, a.rounds)
                for k in cols:
                    agg[size][0][k] += x[k]
                    agg[size][1][k] += y[k]
                agg[size][2] += 1
        for size, (x, y, n) in sorted(agg.items(), reverse=True):
            print(f"v{ver:7s} {size:6s} {n:3d} | " + " ".join(f"{x[c] / n:6.0f}" for c in cols) + " |          " + " ".join(f"{y[c] / n:6.0f}" for c in cols))
    return 0


if __name__ == "__main__":
    sys.exit(main())
