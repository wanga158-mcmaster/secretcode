"""Quota-paced online panel runner for bots/main: create the panel's groups as the rolling quota frees,
then (optionally) keep the quota busy with groups against top-ranked teams, and fetch finished groups.

  python -B bots/main/tools/panelrun.py --panel P.json [--version N] [--hours H] [--fill-top K]

A group costs five matches of the rolling hourly quota; a group is created only while at least five are
free, and the quota is re-read after every creation. Progress lives in P.json.done (entry indices already
created), so a restart never duplicates a group. --fill-top K: once the panel is exhausted, create
groups against a random team ranked 1..K on random maps and sides (data for the studies).
Stop it by creating .cache/main/keeper.pause (the panel resumes when the file is removed).
"""
from __future__ import annotations

import argparse
import json
import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import online  # noqa: E402

MATCHES_PER_GROUP = 1  # the quota counts groups (10 groups -> used 10 of 50), not matches


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--panel", required=True)
    p.add_argument("--version", default="")
    p.add_argument("--hours", type=float, default=12.0)
    p.add_argument("--every", type=float, default=60.0)
    p.add_argument("--fill-top", type=int, default=0)
    a = p.parse_args()
    panel = Path(a.panel)
    entries = json.loads(panel.read_text(encoding="utf-8"))
    done_path = panel.with_name(panel.name + ".done")
    done = set(json.loads(done_path.read_text(encoding="utf-8"))) if done_path.exists() else set()
    rng = random.Random()
    teams, teams_at = [], 0.0
    deadline = time.time() + a.hours * 3600
    while time.time() < deadline:
        try:
            online.fetch(a, max_seconds=online.FETCH_SECONDS)
        except (RuntimeError, OSError, ValueError, KeyError) as e:
            print(f"{time.strftime('%H:%M:%S')} fetch error: {online.brief_error(e)}", flush=True)
        if online.PAUSE.exists():
            print(f"{time.strftime('%H:%M:%S')} paused ({online.PAUSE})", flush=True)
            time.sleep(a.every)
            continue
        try:
            version = a.version or online.live_version()
            free = online.quota_available()
            while free >= MATCHES_PER_GROUP and not online.PAUSE.exists():
                todo = [i for i in range(len(entries)) if i not in done]
                if todo:
                    i = todo[0]
                    e = entries[i]
                    maps, sides, opp, name, rank = ",".join(e["maps"]), ",".join(e["sides"]), e["opponent"], e.get("name", ""), int(e.get("rank") or 0)
                    kind = "panel"
                elif a.fill_top > 0:
                    if not teams or time.time() - teams_at > 3600:
                        teams, teams_at = online.ladder_teams(), time.time()
                    pool = [t for t in teams if t["teamId"] != online.OUR_TEAM and t["rank"] <= a.fill_top]
                    t = rng.choice(pool)
                    mp = rng.sample(online.POOL, 5)
                    maps, sides = ",".join(mp), ",".join(rng.choice("ab") for _ in mp)
                    opp, name, rank, kind, i = t["teamId"], t["name"], int(t["rank"]), "top-fill", None
                else:
                    break
                try:
                    out = online.create_group(opp, maps, sides)
                except online.IneligibleOpponent as exc:
                    print(f"{time.strftime('%H:%M:%S')} ineligible {name}: {online.brief_error(exc)}", flush=True)
                    if i is not None:
                        done.add(i)
                        done_path.write_text(json.dumps(sorted(done)), encoding="utf-8")
                    continue
                groups = online.load()
                online.record(groups, version, out["groupId"], opp, maps, sides, out.get("teamName", name), kind=kind, rank=rank)
                online.save(groups)
                if i is not None:
                    done.add(i)
                    done_path.write_text(json.dumps(sorted(done)), encoding="utf-8")
                print(f"{time.strftime('%H:%M:%S')} created {out['groupId'][:8]} v{version} {kind} vs #{rank} {name} {maps} {sides}", flush=True)
                free = online.quota_available()
        except (RuntimeError, OSError, ValueError, KeyError) as e:
            print(f"{time.strftime('%H:%M:%S')} create error: {online.brief_error(e)}", flush=True)
        time.sleep(a.every)
    return 0


if __name__ == "__main__":
    sys.exit(main())
