"""Play the coordinated siege probe (bots/contenders/_econ-siege) and measure it (economy.md E9, E28).

  python -I run_siege.py NAME MAP --set K=V ... [--rounds N] [--out DIR]
  python -I run_siege.py --measure DIR [--team a]

MAP is a pool map name or a .map26a path (lab maps: data/economy/maps/). The subject plays side A
with the given knobs against the same bot at its passive defaults.

Resume (economy.md E8, E9, E28): the configurations run so far are the batch scripts
.cache/contenders/economy/siege_batch{1,2,3}.sh (mono-v1/v2, mono-v2-vesper, mono-jungle[-vesper],
mono-rejoin, mono-jungle-rejoin, golem-five, golem-trio[-rejoin], elk2-trio, elk1-pair); outputs in
.cache/contenders/economy/siege/<name>/ and <name>.txt. Example:
  python -I run_siege.py mono-v2 ../../data/economy/maps/econ-siege.map26a --set S_ACTIVE=5253
      --set S_GX=14 --set S_GY=9 --set S_RX=6 --set S_RY=3 --rounds 900
Rebuild the lab map with: node tools/mechanics/mapgen.mjs build data/economy/maps/econ-siege.json
data/economy/maps/econ-siege.map26a (from bots/contenders/foundation). Not yet run: a pool-map
monolith (00lost_world g_491, an edge pocket with clusters) and a siege policy that schedules the
last tank by health at arrival. --measure reads the replay's
team-a stdout and prints: waves (start rounds), first and last round the target lost health, the
round its reward state began, champion damage taken, recalls, deaths, covered and uncovered leaves,
and rally waits (first READY of a wave to its WAVE_START).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "tools" / "arena"))

ST = re.compile(r"^R(\d+) (\w+) hp([\d.]+)/(\d+) prev(-?[\d.]+) p(\d+),(\d+) .* ph(\d) st(\d+) w(\d+) rdy(\w+) eng(\w+) \| living(\d+) hp([\d.]+)")
DEAD = re.compile(r"^R(\d+) (\w+) dead")
EV = re.compile(r"^(WAVE_START|WAVE_END|JOIN|LEAVE_COVERED|LEAVE_UNCOVERED|RECALL|READY|GAIN|CLAIM|BUY|INIT) r(\d+)(.*)")


def play(name, mapname, sets, rounds, out):
    from core import bot as find_bot, prepare_many, map_path  # noqa: E402
    from games import GameSpec, play as play_games  # noqa: E402
    subject = find_bot("contenders/_econ-siege")
    overrides = dict(s.split("=", 1) for s in sets)
    overrides.setdefault("S_MODE", "1")
    ents = prepare_many([(subject, overrides, name), (subject, {}, "passive")])
    for e in ents:
        if not e.ok:
            print(f"BUILD FAILED {e.bot.id}\n{e.log[-2000:]}")
            return 1
    mp = Path(mapname)
    mp = mp.resolve() if mp.suffix == ".map26a" else map_path(mapname)
    spec = GameSpec(ents[0], ents[1], str(mp), {"subject": "A", "probe": name})
    rows = play_games([spec], out, rounds=rounds, keep_replays="all", quiet=True, resume=False)
    r = rows[0]
    print(f"== {name}: winner={r.get('winner')} rounds={r.get('rounds')} error={(r.get('error') or '')[:200]} replay={r.get('replay')}")
    return 0


def measure(run: Path, team="a"):
    reps = sorted(run.rglob("*.repl26a"), key=lambda q: q.stat().st_mtime)
    if not reps:
        print("no replay")
        return {}
    with zipfile.ZipFile(reps[-1]) as z:
        text = z.read(f"team-{team}.stdout").decode("utf-8", "replace").replace("\x00", "")
    events = defaultdict(list)
    taken = defaultdict(float)
    first_dmg = last_dmg = reward = None
    full = None
    prev_hp = None
    dead_now = set()
    deaths = 0
    for raw in text.splitlines():
        line = raw.strip()
        m = ST.match(line)
        if m:
            r, who = int(m.group(1)), m.group(2)
            hp, prev = float(m.group(3)), float(m.group(5))
            if prev >= 0 and prev > hp:
                taken[who] += prev - hp
            dead_now.discard(who)
            living, thp = int(m.group(13)), float(m.group(14))
            if full is None and living > 0:
                full = thp
            if full is not None and living > 0 and thp < full - 0.5:
                if first_dmg is None:
                    first_dmg = r
                last_dmg = r
            if living == 0 and full is not None and reward is None and first_dmg is not None:
                reward = r
            continue
        m = DEAD.match(line)
        if m:
            if m.group(2) not in dead_now:
                deaths += 1
                dead_now.add(m.group(2))
            continue
        m = EV.match(line)
        if m:
            events[m.group(1)].append((int(m.group(2)), m.group(3).strip()))
    waves = [r for r, _ in events["WAVE_START"]]
    ready = sorted(r for r, _ in events["READY"])
    waits = []
    prev_start = 0
    for w in waves:
        rs = [r for r in ready if prev_start < r <= w]
        if rs:
            waits.append((w - rs[0], rs[-1] - rs[0]))
        prev_start = w
    res = {"waves": len(waves), "wave_starts": waves, "first_target_damage": first_dmg, "last_target_damage": last_dmg,
           "reward_round": reward, "span": (reward - first_dmg) if reward and first_dmg else None,
           "damage_taken": round(sum(taken.values())), "per_champion": {k: round(v) for k, v in taken.items()},
           "recalls": len(events["RECALL"]) + len(events["LEAVE_COVERED"]), "deaths": deaths,
           "covered_leaves": len(events["LEAVE_COVERED"]), "uncovered_leaves": len(events["LEAVE_UNCOVERED"]),
           "rally_waits": waits, "gains": events["GAIN"][:6], "buys": events["BUY"][:6]}
    print(json.dumps(res))
    return res


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("name", nargs="?")
    p.add_argument("map", nargs="?")
    p.add_argument("--set", action="append", default=[])
    p.add_argument("--rounds", type=int, default=0)
    p.add_argument("--out", default="")
    p.add_argument("--measure", default="")
    a = p.parse_args()
    if a.measure:
        measure(Path(a.measure))
        return 0
    out = Path(a.out or ROOT / ".cache" / "contenders" / "economy" / "siege" / a.name)
    if out.exists() and ".cache" in out.parts:  # regenerable run output: start clean
        import shutil
        shutil.rmtree(out)
    rc = play(a.name, a.map, a.set, a.rounds, out)
    if rc == 0:
        measure(out)
    return rc


if __name__ == "__main__":
    sys.exit(main())
