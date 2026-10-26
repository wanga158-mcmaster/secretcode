"""Scout online teams from public match summaries (bots/main; read-only server commands).

  python -B bots/main/tools/scout.py TEAM [--vs TEAM2] [--limit 40] [--map NAME] [--kind ranked]
TEAM is a team UUID, a full team name or "me". Lists the team's recent matches (`cambc matches
list`), fetches each summary once (`cambc matches summary`, cached in .cache/main/online/summaries/)
and prints one row per game with both teams' economy and fight numbers:
  clears by round 100/150/300, earned gold by round 150/300, kills-deaths, final gems, locks.
`--vs` keeps only games against TEAM2. Mean rows per map size (00 small, 01 large) close the table.
"""
from __future__ import annotations

import argparse
import gzip
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CAMBC = ROOT / ".tools" / "cambc.exe"
CACHE = ROOT / ".cache" / "main" / "online" / "summaries"
NAMES = ["ryth", "vesper", "vendetta", "c&i", "stellar", "prism", "marcella", "willow", "ren", "varg", "tavi", "heart"]


def cli(*args: str, timeout: int = 300) -> str:
    p = subprocess.run([str(CAMBC), *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=timeout)
    if p.returncode:
        raise RuntimeError(f"cambc {' '.join(args)}: {p.stderr.strip() or p.stdout.strip()}")
    return p.stdout


# Engine 1.0.3 summaries (CLI 3.0.4 reading them) name champions "Unknown champion (N)", N being the SDK's
# champion_type value; names() restores them.
CHAMPION_TYPES = ["Ryth", "Vesper Vale", "Vendetta", "Cassi", "Issac", "Stellar", "Prism", "Marcella", "Willow",
                  "Ren", "Varg", "Tavi", "Heart of Ruin"]
UNKNOWN = re.compile(r"Unknown champion \((\d+)\)")


def names(s: dict) -> dict:
    def fix(o):
        if isinstance(o, dict):
            n = o.get("name")
            if isinstance(n, str) and (m := UNKNOWN.fullmatch(n)) and int(m.group(1)) < len(CHAMPION_TYPES):
                o["name"] = CHAMPION_TYPES[int(m.group(1))]
            for v in o.values():
                fix(v)
        elif isinstance(o, list):
            for v in o:
                fix(v)
    fix(s)
    return s


def load_summary(gdir: Path, match_id: str) -> dict | None:
    """A group download's match summary: <match-id>-summary.json.gz (fetch stores them gzipped) or .json."""
    gz, plain = gdir / f"{match_id}-summary.json.gz", gdir / f"{match_id}-summary.json"
    try:
        if gz.exists():
            return names(json.loads(gzip.decompress(gz.read_bytes()).decode("utf-8")))
        if plain.exists():
            return names(json.loads(plain.read_text(encoding="utf-8")))
    except (OSError, ValueError):
        return None
    return None


def summary(match_id: str) -> dict | None:
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / f"{match_id}.json"
    if not path.exists():
        try:
            cli("matches", "summary", match_id, "--json", "--output", str(path))
        except RuntimeError as e:
            print("  summary failed:", match_id, str(e)[:120], file=sys.stderr)
            return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def value_at(runs: list, rnd: int) -> float:
    t, v = 0, 0.0
    for r in runs:
        if t > rnd:
            break
        v = float(r["value"])
        t += r["duration"]
    return v


def team_stats(s: dict, team: str) -> dict:
    ids = {c["id"] for c in s["roster"]["champions"] if c["team"] == team}
    out = defaultdict(float)
    for e in s["events"]:
        owner = e.get("owner") or {}
        if owner.get("id") not in ids or owner.get("kind") != "champion":
            continue
        k = e["kind"]
        for x in e.get("entries", []):
            rnd = x["timestamp"]["round"]
            if k == "campClear":
                for cut in (100, 150, 300):
                    out[f"c{cut}"] += rnd <= cut
                out["clears"] += 1
            elif k == "buyConsumable" and "Lock" in x["target"].get("name", ""):
                out["locks"] += 1
            elif k == "wardPlacement":
                out["wards"] += 1
    for t in s["championTimelines"]:
        if t["owner"].get("id") not in ids:
            continue
        if t["metric"] == "earnedGold":
            out["g150"] += value_at(t["runs"], 150)
            out["g300"] += value_at(t["runs"], 300)
        elif t["metric"] in ("kills", "deaths"):
            out[t["metric"]] += value_at(t["runs"], 10 ** 6)
    gems = next((tt for tt in s["teamTimelines"] if tt["owner"] == team), None)
    out["gems"] = value_at(gems["runs"], 10 ** 6) if gems else 0
    out["gems300"] = value_at(gems["runs"], 300) if gems else 0
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("team")
    p.add_argument("--vs")
    p.add_argument("--limit", type=int, default=40)
    p.add_argument("--map")
    p.add_argument("--kind", default="ranked")
    a = p.parse_args()
    args = ["matches", "list", "--team", a.team, "--limit", str(a.limit), "--json"]
    if a.kind != "all":
        args += ["--kind", a.kind]
    matches = json.loads(cli(*args))["matches"]
    cols = "c100 c150 c300 g150 g300 gems300".split()
    print(f"{'map':22s} {'opponent':18s} res  rnd | " + " ".join(f"{c:>6s}" for c in cols) + "   k-d    gems lk | opp: "
          + " ".join(f"{c:>6s}" for c in cols) + "   k-d    gems lk  draft")
    agg = defaultdict(lambda: defaultdict(float))
    for m in matches:
        if m.get("status") != "finished" or (a.map and m["map"] != a.map):
            continue
        me = next((t for t in m["teams"] if a.team in (t["teamId"], t["name"]) or (a.team == "me" and t["name"] == "ha²t")), None)
        if me is None:
            me = m["teams"][0] if a.team.lower() == m["teams"][0]["name"].lower() else m["teams"][1]
        opp = next(t for t in m["teams"] if t is not me)
        if a.vs and a.vs not in (opp["teamId"], opp["name"]):
            continue
        s = summary(m["matchId"])
        if not s:
            continue
        ours, theirs = ("teamA", "teamB") if me["side"] == "a" else ("teamB", "teamA")
        x, y = team_stats(s, ours), team_stats(s, theirs)
        size = "small" if m["map"].startswith("00") else "large"
        for key in cols + ["kills", "deaths", "gems"]:
            agg[size]["us_" + key] += x[key]
            agg[size]["op_" + key] += y[key]
        agg[size]["n"] += 1
        agg[size]["w"] += me["result"] == "win"
        draft = ",".join(NAMES[c] for c in me.get("draftPicks") or []) + " / " + ",".join(NAMES[c] for c in opp.get("draftPicks") or [])
        print(f"{m['map'][:22]:22s} {opp['name'][:18]:18s} {me['result'][:4]:4s} {s['match']['roundsCompleted']:4d} | "
              + " ".join(f"{x[c]:6.0f}" for c in cols) + f" {x['kills']:3.0f}-{x['deaths']:<3.0f} {x['gems']:5.0f} {x['locks']:2.0f} | opp: "
              + " ".join(f"{y[c]:6.0f}" for c in cols) + f" {y['kills']:3.0f}-{y['deaths']:<3.0f} {y['gems']:5.0f} {y['locks']:2.0f}  {draft}")
    for size, d in agg.items():
        n = d["n"]
        print(f"MEAN {size:6s} n={n:.0f} W{d['w']:.0f} | us " + " ".join(f"{c}={d['us_' + c] / n:.0f}" for c in cols + ['kills', 'deaths', 'gems'])
              + " | opp " + " ".join(f"{c}={d['op_' + c] / n:.0f}" for c in cols + ['kills', 'deaths', 'gems']))
    return 0


if __name__ == "__main__":
    sys.exit(main())
