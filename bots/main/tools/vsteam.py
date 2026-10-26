"""Per-game comparison of us vs the opponent in downloaded online match groups (bots/main).

Usage: python -B bots/main/tools/vsteam.py <group-id-prefix> ... [--allow-online]
Finds each group in results/online/groups.json, reads results/online/<v>/<group>/group.zip and the
match summaries beside it (<match-id>-summary.json) and prints per game: map, result, gems
tributed, clears, kills, deaths (champion/camp), gold, the opponent's roster and top shard
purchases, then per-group means. Missing summaries are fetched (`cambc matches summary`, a
server read-only command) only with --allow-online, which needs an explicit user instruction;
without it those games are skipped. DORMANT like online.py.
"""
import argparse
import json
import re
import subprocess
import sys
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ONLINE = ROOT / "results" / "online"
CAMBC = ROOT / ".tools" / "cambc.exe"


def summaries(gdir: Path, allow_online: bool) -> tuple[list[tuple[int, Path]], int]:
    """(match index, summary path) per downloaded match, and the count of missing summaries."""
    with zipfile.ZipFile(gdir / "group.zip") as z:
        members = sorted(Path(n).stem for n in z.namelist() if n.endswith(".zip"))
    out, missing = [], 0
    for stem in members:
        m = re.match(r"(\d+)-(.+)", stem)
        index, match_id = (int(m.group(1)), m.group(2)) if m else (0, stem)
        target = gdir / f"{match_id}-summary.json"
        if not target.exists() and allow_online:
            subprocess.run([str(CAMBC), "matches", "summary", match_id, "--json", "--output", str(target)],
                           cwd=ROOT, capture_output=True, timeout=180)
        if target.exists():
            out.append((index, target))
        else:
            missing += 1
    return out, missing


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("groups", nargs="+", help="group id prefixes (results/online/groups.json)")
    p.add_argument("--allow-online", action="store_true", help="fetch missing summaries; explicit user instruction only")
    args = p.parse_args()
    registry = json.loads((ONLINE / "groups.json").read_text(encoding="utf-8"))
    for prefix in args.groups:
        g = next((r for r in registry if r["group"].startswith(prefix)), None)
        if not g:
            print("unknown group", prefix)
            continue
        gdir = ONLINE / str(g["version"]) / g["group"]
        if not (gdir / "group.zip").is_file():
            print(f"{g['group']}: not downloaded ({gdir / 'group.zip'})")
            continue
        files, missing = summaries(gdir, args.allow_online)
        maps, side_list = g["maps"].split(","), g["sides"].split(",")
        name = (g.get("created") or {}).get("teamName", g["opponent"][:8])
        print(f"== v{g['version']} vs {name} ({len(files)} games" + (f", {missing} without summaries" if missing else "") + ")")
        total = Counter()
        base = min((i for i, _ in files), default=0)
        for index, f in files:
            s = json.loads(f.read_text(encoding="utf-8"))
            match = s["match"]
            k = index - base
            map_name = maps[k] if 0 <= k < len(maps) else "?"
            side = side_list[k] if 0 <= k < len(side_list) else None
            ours = "teamA" if side == "a" else "teamB" if side == "b" else None
            if ours is None:
                continue
            theirs = "teamB" if ours == "teamA" else "teamA"
            roster = {c["id"]: (c["team"], c["name"]) for c in s["roster"]["champions"]}
            m = defaultdict(Counter)
            for c in s["champions"]:
                team = roster[c["id"]][0]
                for key in ("kills", "deaths", "submittedGems", "earnedGold"):
                    m[team][key] += int(float(c["metrics"][key]))
            clears, camp_deaths, shards = Counter(), Counter(), Counter()
            for ev in s["events"]:
                team = ev.get("owner", {}).get("team")
                if ev["kind"] == "campClear":
                    clears[team] += len(ev["entries"])
                if ev["kind"] == "death":
                    camp_deaths[team] += sum(1 for e in ev["entries"] if e.get("context") == "camp")
                if ev["kind"] == "buyItem" and team == theirs:
                    for e in ev["entries"]:
                        shards[e["target"]["name"]] += int(e["value"])
            won = match.get("outcome") == ours
            names = sorted(n for t, n in roster.values() if t == theirs)
            print(f"  {map_name:22} {'W' if won else 'L'} gems {m[ours]['submittedGems']:4}-{m[theirs]['submittedGems']:4}"
                  f" clears {clears[ours]:3}-{clears[theirs]:3} k/d {m[ours]['kills']:2}/{m[ours]['deaths']:2}"
                  f" (camp {camp_deaths[ours]}) gold {m[ours]['earnedGold']:5}-{m[theirs]['earnedGold']:5}"
                  f" r{match.get('roundsCompleted')}")
            print(f"      them: {', '.join(names)}; top shards {dict(shards.most_common(2))}")
            for key, v in (("w", won), ("g", m[ours]["submittedGems"]), ("og", m[theirs]["submittedGems"]),
                           ("c", clears[ours]), ("oc", clears[theirs]), ("k", m[ours]["kills"]), ("d", m[ours]["deaths"])):
                total[key] += v
            total["n"] += 1
        n = max(1, total["n"])
        print(f"  mean: W {total['w']}/{total['n']} gems {total['g'] / n:.0f}-{total['og'] / n:.0f} clears "
              f"{total['c'] / n:.0f}-{total['oc'] / n:.0f} k/d {total['k'] / n:.1f}/{total['d'] / n:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
