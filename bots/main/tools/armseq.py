"""Unattended online collection for bots/main: A/B arms on a fixed panel plus a mixed-rank survey.

  python -B bots/main/tools/armseq.py --panel P.json --arms main-v49,main-v44 [--leave] [--fill 15] [--collect]
      [--first-live] [--hours H]

For each release in turn: wait until none of our matches is queued or running, submit it, record it
live, create the panel's groups (the A/B arm) plus --fill survey groups against opponents drawn
across ranks (top 20 expose strategic weaknesses, lower ranks pointed gameplay flaws), and move on.
--leave submits the last arm without a panel; --collect then keeps the quota busy with survey groups
for the live version until the deadline. Registry rows carry the arm's version (release -vN) and the
opponent's rank. A keeper.pause file pauses creation. Single registry writer while it runs.
"""
from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import online  # noqa: E402

ROOT = online.ROOT
ARENA = ROOT / "arena.cmd"
# Survey opponents: rank range and share. Higher ranks show strategy, lower ranks gameplay flaws.
MIX = [(1, 20, 0.35), (21, 60, 0.30), (61, 250, 0.35)]


def log(msg: str) -> None:
    print(f"{time.strftime('%H:%M:%S')} {msg}", flush=True)


def busy() -> int:
    """Number of our matches still queued or running (most recent 100)."""
    text = online.cli("matches", "list", "--team", "me", "--limit", "100", timeout=60)
    rows = [line.split() for line in text.splitlines() if line and not line[0].isspace()]
    if not rows:
        raise ValueError("empty match listing")
    return sum(1 for r in rows if r[1:2] in (["queued"], ["running"]))


def fetch_some() -> None:
    try:
        online.fetch(argparse.Namespace(), max_seconds=online.FETCH_SECONDS)
    except (RuntimeError, OSError, ValueError, KeyError) as e:
        log(f"fetch error: {online.brief_error(e)}")


def wait_idle(deadline: float) -> bool:
    idle = 0  # two consecutive empty listings (an empty reply under load must not look idle)
    while time.time() < deadline:
        fetch_some()
        try:
            n = busy()
        except (RuntimeError, OSError, ValueError) as e:
            log(f"list error: {online.brief_error(e)}")
            n = -1
        if n == 0:
            idle += 1
            if idle >= 2:
                return True
            time.sleep(20)
            continue
        idle = 0
        log(f"waiting: {n} match(es) queued or running")
        time.sleep(60)
    return False


class Creator:
    def __init__(self, deadline: float):
        self.deadline = deadline
        self.rng = random.Random()
        self.teams, self.teams_at, self.recent = [], 0.0, []

    def ladder(self) -> list:
        while not self.teams or time.time() - self.teams_at > 3600:
            try:
                self.teams, self.teams_at = online.ladder_teams(), time.time()
            except (RuntimeError, OSError, ValueError) as e:  # rate limits and timeouts: retry
                log(f"ladder error: {online.brief_error(e)}")
                if self.teams:
                    self.teams_at = time.time() - 3000  # keep the old ladder; retry in ~10 minutes
                    break
                time.sleep(30)
        return self.teams

    def survey_entry(self) -> dict:
        teams = [t for t in self.ladder() if t["teamId"] != online.OUR_TEAM]
        r = self.rng.random() * sum(s for _, _, s in MIX)
        lo, hi = MIX[-1][:2]
        for a, b, s in MIX:
            if r < s:
                lo, hi = a, b
                break
            r -= s
        pool = [t for t in teams if lo <= t["rank"] <= hi and t["teamId"] not in self.recent] or \
               [t for t in teams if lo <= t["rank"] <= hi]
        t = self.rng.choice(pool)
        self.recent = (self.recent + [t["teamId"]])[-40:]
        maps = self.rng.sample(online.POOL, 5)
        return {"opponent": t["teamId"], "name": t["name"], "rank": t["rank"], "maps": maps,
                "sides": [self.rng.choice("ab") for _ in maps]}

    def create(self, entries: list, version: str, kind: str) -> int:
        made, todo = 0, list(entries)
        while todo and time.time() < self.deadline:
            while online.PAUSE.exists():
                log("paused")
                time.sleep(60)
            try:
                free = online.quota_available()
            except (RuntimeError, OSError, ValueError) as e:
                log(f"quota error: {online.brief_error(e)}")
                time.sleep(60)
                continue
            while todo and free >= 1:
                e = todo[0]
                try:
                    out = online.create_group(e["opponent"], ",".join(e["maps"]), ",".join(e["sides"]))
                except online.IneligibleOpponent as exc:
                    log(f"ineligible {e.get('name')}: {online.brief_error(exc)}")
                    todo.pop(0)
                    continue
                except RuntimeError as exc:
                    log(f"create error: {online.brief_error(exc)}")
                    break
                groups = online.load()
                online.record(groups, version, out["groupId"], e["opponent"], ",".join(e["maps"]), ",".join(e["sides"]),
                              out.get("teamName", e.get("name", "")), kind=kind, rank=int(e.get("rank") or 0))
                online.save(groups)
                made += 1
                todo.pop(0)
                free -= 1
            if todo:
                time.sleep(60)
        return made


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--panel", required=True)
    p.add_argument("--arms", required=True)
    p.add_argument("--hours", type=float, default=8.0)
    p.add_argument("--leave", action="store_true", help="submit the last arm and leave it live without a panel")
    p.add_argument("--fill", type=int, default=15, help="mixed-rank survey groups per arm")
    p.add_argument("--collect", action="store_true", help="after the arms, keep collecting survey groups for the live version")
    p.add_argument("--first-live", action="store_true", help="the first arm is already live with its panel created: only its survey")
    a = p.parse_args()
    entries = json.loads(Path(a.panel).read_text(encoding="utf-8"))
    deadline = time.time() + a.hours * 3600
    maker = Creator(deadline)
    arms = [x for x in a.arms.split(",") if x]
    for k, arm in enumerate(arms):
        version = arm.rsplit("-v", 1)[-1]
        last = k == len(arms) - 1
        if k == 0 and a.first_live:
            log(f"{arm}: already live; created {maker.create([maker.survey_entry() for _ in range(a.fill)], version, 'survey')} survey groups")
            if last:
                break
            continue
        if not wait_idle(deadline):
            log("deadline while waiting for an idle queue; stopping")
            return 3
        while online.PAUSE.exists():
            log("paused")
            time.sleep(60)
        log(f"submitting {arm}")
        # Never pass -n/--note/--name: the bot's name on the website stays as it is (user rule, D333).
        sub = subprocess.run([str(online.CAMBC), "submit", f"anchors/{arm}"], cwd=ROOT,
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        text = (sub.stdout + sub.stderr).strip()
        log(text.splitlines()[-1] if text else f"submit exit {sub.returncode}")
        if sub.returncode != 0:
            log("submit failed; stopping")
            return 2
        live = subprocess.run([str(ARENA), "release", "--live", arm], cwd=ROOT, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        text = (live.stdout + live.stderr).strip()
        log(text.splitlines()[-1] if text else "release --live done")
        if not (last and a.leave):
            log(f"{arm}: created {maker.create(entries, version, 'arm')} panel groups")
        if a.fill:
            log(f"{arm}: created {maker.create([maker.survey_entry() for _ in range(a.fill)], version, 'survey')} survey groups")
        if last:
            break
    if a.collect:
        version = arms[-1].rsplit("-v", 1)[-1]
        while time.time() < deadline:
            fetch_some()
            n = maker.create([maker.survey_entry() for _ in range(5)], version, "survey")
            log(f"collect: created {n} survey groups")
            time.sleep(120)
    wait_idle(deadline)
    log("sequence finished")
    return 0


if __name__ == "__main__":
    sys.exit(main())
