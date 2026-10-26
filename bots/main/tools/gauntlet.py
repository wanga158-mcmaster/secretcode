"""Archetype gauntlet for bots/main: our bot (and knob variants) against strategy personas and anchors.

  python -B bots/main/tools/gauntlet.py [--bot-dir DIR] [--var "NAME: k=v ..."] [--personas all|a,b] [--anchors SEL]
         [--maps pool] [--sides both] [--seeds N] [--out DIR] [--max-minutes M] [--keep-replays all] [--list]

The opponents are not today's ladder: each persona is main/main built from the same source snapshot
with knobs that make it play a distinct strategy (early aggression, gem race, grouped squads, burst
builds, the online meta comp), plus fixed anchors. A change should hold up against every archetype,
not only on average (README "Evaluation and release", D89). The report is arena's sweep report
(paired flips, McNemar, gem margins, by opponent, health). Personas replay their old games bit for
bit only while the source is unchanged; anchors are the fixed reference.
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import ArenaError, apply_overrides, map_set, prepare_many, read_closure, select, version_of  # noqa: E402
from core import bot as find_bot  # noqa: E402
from games import GameSpec, Incomplete, deadline_from, play  # noqa: E402
from sweep import BASE, out_dir, parse_var, render, seed_list, sweep_report  # noqa: E402
import core  # noqa: E402


@dataclass(frozen=True, order=True)
class DirBot(core.Bot):
    """A bot built from any directory under the repository (a workbench copy with its own src/)."""
    root: str = ""

    @property
    def path(self) -> Path:
        return Path(self.root)


def subject_bot(bot: str, bot_dir: str):
    if bot_dir:
        d = Path(bot_dir).resolve()
        if not (d / "src" / "main.cpp").is_file():
            raise ArenaError(f"{d} has no src/main.cpp")
        return DirBot("workbench", d.name, str(d))
    return find_bot(bot)

# Strategy archetypes as knob sets of main/main (config.h). Keep them strategically distinct.
PERSONAS = {
    "hunter": {"hunt_mode": "1", "hunt_radius": "20", "hunt_health": "0.4f", "fight_better_trades_only": "false",
               "limited_chase": "false"},
    "rush": {"gem_value": "8.0f", "gem_value_late": "8.0f", "gem_rich_value": "8.0f", "tribute_gems": "10"},
    "squads": {"squad_size": "3"},
    "burst": {"focus_builds": "true", "draft_mode": "16"},
    "meta": {"draft_mode": "19", "shop_plan": "14", "early_shop_gold": "90", "early_shop_until": "400"},
    # From the ecosystem catalog (.cache/main/analysis/ecosystem/catalog.md):
    "brawl": {"squad_size": "3", "focus_builds": "true", "draft_mode": "16", "hunt_mode": "1", "hunt_radius": "12"},
    "willow": {"draft_mode": "12", "squad_size": "2"},
    "hoarder": {"tribute_gems": "120"},
    # D330: a local stand-in for grouped hunters (Intercontinental Fish opens as a 6-body ball and
    # kills lone farmers): the escorted ball all game, hunting what it sees, taking even trades.
    "deathball": {"escort_until": "2000", "squad_size": "9", "squad_until_round": "2000", "hunt_mode": "1",
                  "hunt_radius": "20", "hunt_health": "0.5f", "fight_better_trades_only": "false", "limited_chase": "false",
                  "carrier_capture": "true", "draft_mode": "19"},
    "deathball2": {"escort_until": "2000", "squad_size": "9", "squad_until_round": "2000", "hunt_mode": "2",
                   "hunt_radius": "20", "hunt_health": "0.5f", "carrier_capture": "true", "draft_mode": "19"},
}
DEFAULT_ANCHORS = "anchors/main-v28,anchors/main-hunter7,anchors/starter-cpp"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bot", default="main/main")
    p.add_argument("--bot-dir", default="", help="build the subject from this directory (a workbench copy with src/)")
    p.add_argument("--var", action="append", default=[], help='a variant of the subject: "NAME: k=v ..." (repeatable)')
    p.add_argument("--personas", default="all")
    p.add_argument("--anchors", default=DEFAULT_ANCHORS, help="fixed opponents (selectors); '' for none")
    p.add_argument("--maps", default="pool")
    p.add_argument("--sides", choices=["both", "A", "B"], default="both")
    p.add_argument("--seeds", type=int, default=0)
    p.add_argument("--seed-knob", default="noise_burn")
    p.add_argument("--jobs", type=int, default=0)
    p.add_argument("--out", default="")
    p.add_argument("--max-minutes", type=float, default=0)
    p.add_argument("--keep-replays", choices=["flagged", "all", "none"], default="flagged")
    p.add_argument("--list", action="store_true", help="print the personas and exit")
    a = p.parse_args()
    if a.list:
        for name, ov in PERSONAS.items():
            print(f"{name:8s} " + " ".join(f"{k}={v}" for k, v in ov.items()))
        return 0
    subject = subject_bot(a.bot, a.bot_dir)
    names = list(PERSONAS) if a.personas == "all" else [x for x in a.personas.split(",") if x and x != "none"]
    unknown = [x for x in names if x not in PERSONAS]
    if unknown:
        raise ArenaError(f"unknown persona(s): {', '.join(unknown)} (see --list)")
    variants = [parse_var(v) for v in a.var]
    seeds = seed_list(a.seeds)
    maps = map_set(a.maps)
    sides = ["A", "B"] if a.sides == "both" else [a.sides]
    contents = read_closure(subject)
    version = version_of(contents)
    plan = []
    for v, ov in [(BASE, {})] + variants:
        for s in seeds:
            full = dict(ov, **({a.seed_knob: str(s)} if s else {}))
            apply_overrides(subject, contents, full)
            plan.append((v, s, full))
    for n in names:
        apply_overrides(subject, contents, PERSONAS[n])
    anchors = select(a.anchors) if a.anchors else []
    out = out_dir(a.out, "sweeps", "gauntlet")
    items = [(subject, full, f"{subject.id}:{v}", contents) for v, s, full in plan]
    items += [(subject, PERSONAS[n], f"persona:{n}", contents) for n in names]
    items += [(o, None, "", None) for o in anchors]
    print(f"gauntlet {subject.id}@{version} -> {out}\n  variants: {', '.join([BASE] + [v for v, _ in variants])}"
          f"\n  personas: {', '.join(names) or '-'}\n  anchors: {', '.join(o.id for o in anchors) or '-'}"
          f"\n  {len(maps)} map(s), sides {'/'.join(sides)}, {'seeds 1..%d' % a.seeds if a.seeds else 'unseeded'}", flush=True)
    ents = prepare_many(items)
    mine = {(v, s): e for (v, s, _), e in zip(plan, ents)}
    opps = [(f"persona:{n}", e) for n, e in zip(names, ents[len(plan):len(plan) + len(names)])]
    opps += [(o.id, e) for o, e in zip(anchors, ents[len(plan) + len(names):])]
    for key, e in {e.key: e for e in ents if not e.ok}.items():
        print(f"FAILED build {key}", flush=True)
    specs = []
    for label, o in opps:
        for m in maps:
            for side in sides:
                for s in seeds:
                    for v, _ in [(BASE, {})] + variants:
                        e = mine[(v, s)]
                        x, y = (e, o) if side == "A" else (o, e)
                        specs.append(GameSpec(x, y, m, {"subject": side, "variant": v, "opponent": label, "seed": s,
                                                        "phase": "sweep"}))
    rows = play(specs, out, jobs=a.jobs, keep_replays=a.keep_replays, resume=True, deadline=deadline_from(a.max_minutes))
    secs, results = sweep_report(rows, [BASE] + [v for v, _ in variants])
    (out / "gauntlet.json").write_text(json.dumps({"bot": subject.id, "version": version, "personas": {n: PERSONAS[n] for n in names},
                                                   "anchors": [o.id for o in anchors], "maps": maps, "sides": sides,
                                                   "variants": dict(variants), "results": results}, indent=1), encoding="utf-8")
    print("\n".join(render(secs, terminal=True)))
    print(f"out {out}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Incomplete as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(3)
