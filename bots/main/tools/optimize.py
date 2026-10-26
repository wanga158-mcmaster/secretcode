"""Joint knob optimizer for bots/main: cross-entropy search on the local archetype gauntlet.

  python -B bots/main/tools/optimize.py [--generations 12] [--population 10] [--elite 4]
         [--opponents persona:hunter,persona:meta,persona:hoarder,persona:squads,anchors/main-v28]
         [--maps pool] [--seed-knob noise_burn --seeds 0] [--out .cache/main/opt/run1] [--resume]

One-knob sweeps (D49, D63, D79) found each tunable at a flat optimum on its own; this searches the
joint space. The source snapshot is read once, so edits to main/main during a run do not leak in.
Every candidate (knob overrides of main/main) plays the same games: the opponents x maps x sides set
(personas are main/main with gauntlet.py's persona knobs, anchors are frozen). Score = wins + gem
margin / 2000 per game, minus 5 per game with a fault, fuel exhaustion, error line or stall.
Cross-entropy method on knobs normalised to [0, 1]: sample `population` candidates from N(mean, sd)
(the current defaults always included in generation 0), keep the `elite`, move the mean and sd to
theirs (smoothed). State and every evaluation go to OUT (state.json, evals.jsonl, games/). The best
configuration must then be confirmed on other seeds, legacy maps and the full gauntlet before use.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import apply_overrides, map_set, prepare_many, read_closure, select, version_of  # noqa: E402
from core import bot as find_bot  # noqa: E402
from games import GameSpec, play  # noqa: E402
import gauntlet  # noqa: E402

# (knob, kind, low, high): continuous policy knobs with known sensitivity (config.h).
SPACE = [
    ("recall_health_frac", "f", 0.30, 0.70),
    ("disengage_health_frac", "f", 0.15, 0.45),
    ("camp_safety_margin", "f", 1.10, 2.20),
    ("engage_progress", "f", 0.15, 0.60),
    ("tribute_gems", "i", 10, 60),
    ("tribute_detour_tiles", "i", 2, 16),
    ("shop_recall_gold", "i", 120, 600),
    ("shop_recall_detour", "i", 5, 40),
    ("gem_value", "f", 0.8, 3.0),
    ("retarget_ratio", "f", 1.05, 1.80),
    ("fight_margin", "f", 0.0, 1.5),
    ("retreat_ally_pull", "i", 0, 6),
    ("spawn_hold_radius", "i", 4, 12),
    ("skirmish_horizon", "i", 8, 24),
    ("hp_cost_weight", "f", 0.0, 2.0),
    ("travel_recall_margin", "i", 0, 12),
]
TIED = {"gem_value": ["gem_value_late"]}  # knobs that move together


def defaults(contents: dict, subject) -> np.ndarray:
    """Current values of the SPACE knobs, normalised, read from config.h in the snapshot."""
    import re
    text = next(v for k, v in contents.items() if str(k).endswith("config.h")).decode("utf-8")
    out = []
    for name, kind, lo, hi in SPACE:
        m = re.search(rf"inline constexpr \w+(?:::\w+)? {name} = ([0-9.]+)f?;", text)
        v = float(m.group(1)) if m else (lo + hi) / 2
        out.append((v - lo) / (hi - lo))
    return np.clip(np.array(out), 0.0, 1.0)


def overrides(x: np.ndarray) -> dict[str, str]:
    ov = {}
    for (name, kind, lo, hi), u in zip(SPACE, np.clip(x, 0.0, 1.0)):
        v = lo + float(u) * (hi - lo)
        ov[name] = str(int(round(v))) if kind == "i" else f"{v:.3f}f"
        for t in TIED.get(name, []):
            ov[t] = ov[name]
    return ov


def score(rows: list[dict]) -> tuple[float, dict]:
    s, w, n, bad = 0.0, 0, 0, 0
    for r in rows:
        side = r["tags"]["subject"]
        other = "B" if side == "A" else "A"
        me, them = r.get(side) or {}, r.get(other) or {}
        n += 1
        win = r.get("winner") == side
        w += win
        s += win + (float(me.get("gems", 0)) - float(them.get("gems", 0))) / 2000.0
        if any(me.get(k) for k in ("faults", "exhausted", "errors", "stalls")):
            bad += 1
            s -= 5.0
    return s, {"wins": w, "games": n, "bad": bad}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bot", default="main/main")
    p.add_argument("--generations", type=int, default=12)
    p.add_argument("--population", type=int, default=10)
    p.add_argument("--elite", type=int, default=4)
    p.add_argument("--sd", type=float, default=0.15)
    p.add_argument("--opponents", default="persona:hunter,persona:meta,persona:hoarder,persona:squads,anchors/main-v28")
    p.add_argument("--maps", default="pool")
    p.add_argument("--out", default=str(ROOT / ".cache" / "main" / "opt" / "run1"))
    p.add_argument("--resume", action="store_true")
    p.add_argument("--jobs", type=int, default=0, help="machine slots to use (0: all free)")
    a = p.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    subject = find_bot(a.bot)
    state_path = out / "state.json"
    if a.resume and state_path.exists():
        state = json.loads(state_path.read_text(encoding="utf-8"))
        snap = {Path(k): bytes.fromhex(v) for k, v in state["snapshot"].items()}
    else:
        snap = read_closure(subject)
        state = {"gen": 0, "mean": None, "sd": None, "best": None,
                 "snapshot": {str(k): v.hex() for k, v in snap.items()}}
    contents = snap
    version = version_of(contents)
    state.setdefault("seed", int(time.time()))  # per-generation streams: a resumed generation resamples the same candidates
    state_path.write_text(json.dumps(state), encoding="utf-8")
    maps = map_set(a.maps)
    opp_labels = [o.strip() for o in a.opponents.split(",") if o.strip()]
    persona_items, anchor_bots = [], []
    for o in opp_labels:
        if o.startswith("persona:"):
            persona_items.append((subject, gauntlet.PERSONAS[o.split(":", 1)[1]], o, contents))
        else:
            anchor_bots.append(o)
    anchors = select(",".join(anchor_bots)) if anchor_bots else []
    opp_ents = prepare_many(persona_items + [(b, None, "", None) for b in anchors])
    opps = list(zip([it[2] for it in persona_items] + [b.id for b in anchors], opp_ents))
    mean = np.array(state["mean"]) if state["mean"] is not None else defaults(contents, subject)
    sd = np.array(state["sd"]) if state["sd"] is not None else np.full(len(SPACE), a.sd)
    print(f"optimize {subject.id}@{version}: {len(SPACE)} knobs, {len(opps)} opponents x {len(maps)} maps x 2 sides, "
          f"population {a.population}, elite {a.elite}, out {out}", flush=True)
    evals = out / "evals.jsonl"
    while state["gen"] < a.generations:
        g = state["gen"]
        rng = np.random.default_rng([state["seed"], g])
        xs = [np.clip(mean + sd * rng.standard_normal(len(SPACE)), 0.0, 1.0) for _ in range(a.population)]
        if g == 0:
            xs[0] = mean.copy()  # the current defaults, as the reference point
        items = [(subject, overrides(x), f"{subject.id}:g{g}c{i}", contents) for i, x in enumerate(xs)]
        for _, ov, _, _ in items:
            apply_overrides(subject, contents, ov)
        ents = prepare_many(items)
        specs = []
        for i, e in enumerate(ents):
            for label, o in opps:
                for m in maps:
                    for side in ("A", "B"):
                        x, y = (e, o) if side == "A" else (o, e)
                        specs.append(GameSpec(x, y, m, {"subject": side, "variant": f"g{g}c{i}", "opponent": label,
                                                        "seed": 0, "phase": "opt"}))
        rows = play(specs, out, jobs=a.jobs, resume=True)
        results = []
        for i, x in enumerate(xs):
            mine = [r for r in rows if r["tags"].get("variant") == f"g{g}c{i}"]
            s, info = score(mine)
            results.append((s, i))
            with evals.open("a", encoding="utf-8") as fh:
                fh.write(json.dumps({"gen": g, "cand": i, "score": s, **info, "x": [round(float(v), 4) for v in x],
                                     "overrides": overrides(x), "built": ents[i].ok}) + "\n")
            if state["best"] is None or s > state["best"]["score"]:
                state["best"] = {"score": s, **info, "gen": g, "cand": i, "overrides": overrides(x)}
        results.sort(reverse=True)
        elite = np.array([xs[i] for _, i in results[:a.elite]])
        mean = 0.7 * elite.mean(axis=0) + 0.3 * mean
        sd = np.maximum(0.03, 0.7 * elite.std(axis=0) + 0.3 * sd)
        state.update(gen=g + 1, mean=mean.tolist(), sd=sd.tolist())
        state_path.write_text(json.dumps(state), encoding="utf-8")
        top = results[0]
        print(f"gen {g}: best {top[0]:.1f} (cand {top[1]}), mean of elite {np.mean([s for s, _ in results[:a.elite]]):.1f}, "
              f"worst {results[-1][0]:.1f}; overall best {state['best']['score']:.1f} {state['best']['wins']}/{state['best']['games']}",
              flush=True)
    print("best:", json.dumps(state["best"]), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
