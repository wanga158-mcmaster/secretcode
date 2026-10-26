"""Structure search for bots/main: draft order and shop plan by cross-entropy over categorical choices.

  python -B bots/main/tools/search.py [--generations 10] [--population 12] [--elite 4]
         [--what draft,shop] [--opponents persona:hunter,persona:meta,persona:burst,persona:willow,persona:squads,anchors/main-v28]
         [--maps pool] [--out .cache/main/opt/struct1] [--resume]

The draft (draft_mode 99: the first five entries of draft_pref, distinct champions in pick order) and
the melee shop plan (shop_plan 99: shop_seq, five-shard steps of (family, stat) then the unlimited
stacking step) were fixed choices (D9, D42, D54, D55). Each slot gets a categorical distribution,
peaked on the current choice; candidates are sampled (generation 0 includes the current
configuration as the reference), every candidate plays the same paired games as tools/optimize.py
(opponents include different drafts so the search does not tune to mirror matches), the elite
reshape the distributions (smoothed frequencies). Score and health penalty as in optimize.py.
Output: OUT/state.json, OUT/evals.jsonl, games. Confirm the winner on seeds, legacy maps and the
full gauntlet before use.
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
from optimize import score  # noqa: E402

CHAMPS = 12  # champion_draft values 0..11
DRAFT_SLOTS = 5
DEFAULT_DRAFT = [11, 6, 0, 2, 9]
# Shop steps: (family, stat) with family 0 monster 1 power 2 durability 3 treasure 4 jungle 5 home
# 6 bruiser; stat 0 strength 1 focus 2 health 3 main. Five shards each; then the unlimited step.
STEP_CHOICES = [(f, s) for f in range(7) for s in range(4)]
SHOP_SLOTS = 5
DEFAULT_SHOP = [(3, 3), (4, 3), (2, 2), (0, 3), (6, 2)]
FINAL_CHOICES = [(6, 3), (1, 3), (6, 2), (1, 2)]
DEFAULT_FINAL = (6, 3)


def peaked(n: int, idx: int, mass: float = 0.5) -> np.ndarray:
    p = np.full(n, (1.0 - mass) / (n - 1))
    p[idx] = mass
    return p


def sample_draft(dist: list, rng) -> list[int]:
    picks = []
    for slot in dist:
        p = np.array(slot, dtype=float)
        p[picks] = 0.0
        p = p / p.sum()
        picks.append(int(rng.choice(CHAMPS, p=p)))
    return picks


def overrides(draft: list[int] | None, shop: list[int] | None, final: int | None) -> dict[str, str]:
    ov = {}
    if draft is not None:
        rest = [c for c in range(CHAMPS) if c not in draft]
        ov["draft_mode"] = "99"
        ov["draft_pref"] = "{ " + ", ".join(str(c) for c in draft + rest) + " }"
    if shop is not None:
        steps = [STEP_CHOICES[i][0] * 100 + STEP_CHOICES[i][1] * 10 + 5 for i in shop]
        f = FINAL_CHOICES[final]
        steps.append(f[0] * 100 + f[1] * 10)
        steps += [-1] * (8 - len(steps))
        ov["shop_plan"] = "99"
        ov["shop_seq"] = "{ " + ", ".join(str(s) for s in steps) + " }"
    return ov


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bot", default="main/main")
    p.add_argument("--generations", type=int, default=10)
    p.add_argument("--population", type=int, default=12)
    p.add_argument("--elite", type=int, default=4)
    p.add_argument("--what", default="draft,shop")
    p.add_argument("--opponents", default="persona:hunter,persona:meta,persona:burst,persona:willow,persona:squads,anchors/main-v28")
    p.add_argument("--maps", default="pool")
    p.add_argument("--out", default=str(ROOT / ".cache" / "main" / "opt" / "struct1"))
    p.add_argument("--resume", action="store_true")
    p.add_argument("--jobs", type=int, default=0, help="machine slots to use (0: all free)")
    a = p.parse_args()
    what = set(a.what.split(","))
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    subject = find_bot(a.bot)
    state_path = out / "state.json"
    if a.resume and state_path.exists():
        state = json.loads(state_path.read_text(encoding="utf-8"))
        contents = {Path(k): bytes.fromhex(v) for k, v in state["snapshot"].items()}
    else:
        contents = read_closure(subject)
        state = {"gen": 0, "best": None, "snapshot": {str(k): v.hex() for k, v in contents.items()},
                 "draft": [peaked(CHAMPS, c).tolist() for c in DEFAULT_DRAFT],
                 "shop": [peaked(len(STEP_CHOICES), STEP_CHOICES.index(s)).tolist() for s in DEFAULT_SHOP],
                 "final": peaked(len(FINAL_CHOICES), FINAL_CHOICES.index(DEFAULT_FINAL)).tolist()}
    version = version_of(contents)
    state.setdefault("seed", int(time.time()))  # per-generation streams: a resumed generation resamples the same candidates
    state_path.write_text(json.dumps(state), encoding="utf-8")
    maps = map_set(a.maps)
    labels = [o.strip() for o in a.opponents.split(",") if o.strip()]
    persona_items = [(subject, gauntlet.PERSONAS[o.split(":", 1)[1]], o, contents) for o in labels if o.startswith("persona:")]
    anchors = select(",".join(o for o in labels if not o.startswith("persona:"))) if any(not o.startswith("persona:") for o in labels) else []
    opp_ents = prepare_many(persona_items + [(b, None, "", None) for b in anchors])
    opps = list(zip([it[2] for it in persona_items] + [b.id for b in anchors], opp_ents))
    print(f"search {sorted(what)} {subject.id}@{version}: {len(opps)} opponents x {len(maps)} maps x 2 sides, "
          f"population {a.population}, elite {a.elite}, out {out}", flush=True)
    evals = out / "evals.jsonl"
    while state["gen"] < a.generations:
        g = state["gen"]
        rng = np.random.default_rng([state["seed"], g])
        cands = []
        for i in range(a.population):
            if g == 0 and i == 0:
                d = DEFAULT_DRAFT[:]
                s = [STEP_CHOICES.index(x) for x in DEFAULT_SHOP]
                f = FINAL_CHOICES.index(DEFAULT_FINAL)
            else:
                d = sample_draft(state["draft"], rng)
                s = [int(rng.choice(len(STEP_CHOICES), p=np.array(q) / np.sum(q))) for q in state["shop"]]
                f = int(rng.choice(len(FINAL_CHOICES), p=np.array(state["final"]) / np.sum(state["final"])))
            cands.append((d if "draft" in what else None, s if "shop" in what else None, f if "shop" in what else None))
        items = [(subject, overrides(*c), f"{subject.id}:s{g}c{i}", contents) for i, c in enumerate(cands)]
        for _, ov, _, _ in items:
            apply_overrides(subject, contents, ov)
        ents = prepare_many(items)
        specs = []
        for i, e in enumerate(ents):
            for label, o in opps:
                for m in maps:
                    for side in ("A", "B"):
                        x, y = (e, o) if side == "A" else (o, e)
                        specs.append(GameSpec(x, y, m, {"subject": side, "variant": f"s{g}c{i}", "opponent": label,
                                                        "seed": 0, "phase": "struct"}))
        rows = play(specs, out, jobs=a.jobs, resume=True)
        results = []
        for i, c in enumerate(cands):
            mine = [r for r in rows if r["tags"].get("variant") == f"s{g}c{i}"]
            sc, info = score(mine)
            results.append((sc, i))
            rec = {"gen": g, "cand": i, "score": sc, **info, "draft": c[0], "shop": [STEP_CHOICES[j] for j in c[1]] if c[1] else None,
                   "final": FINAL_CHOICES[c[2]] if c[2] is not None else None, "overrides": overrides(*c), "built": ents[i].ok}
            with evals.open("a", encoding="utf-8") as fh:
                fh.write(json.dumps(rec) + "\n")
            if state["best"] is None or sc > state["best"]["score"]:
                state["best"] = rec
        results.sort(reverse=True)
        elite = [cands[i] for _, i in results[:a.elite]]
        if "draft" in what:
            for k in range(DRAFT_SLOTS):
                freq = np.bincount([c[0][k] for c in elite], minlength=CHAMPS) / len(elite)
                state["draft"][k] = (0.6 * freq + 0.4 * np.array(state["draft"][k]) + 0.01).tolist()
        if "shop" in what:
            for k in range(SHOP_SLOTS):
                freq = np.bincount([c[1][k] for c in elite], minlength=len(STEP_CHOICES)) / len(elite)
                state["shop"][k] = (0.6 * freq + 0.4 * np.array(state["shop"][k]) + 0.005).tolist()
            freq = np.bincount([c[2] for c in elite], minlength=len(FINAL_CHOICES)) / len(elite)
            state["final"] = (0.6 * freq + 0.4 * np.array(state["final"]) + 0.01).tolist()
        state["gen"] = g + 1
        state_path.write_text(json.dumps(state), encoding="utf-8")
        top = results[0]
        print(f"gen {g}: best {top[0]:.1f} {cands[top[1]][0]} | elite mean {np.mean([s for s, _ in results[:a.elite]]):.1f}; "
              f"overall best {state['best']['score']:.1f} {state['best']['wins']}/{state['best']['games']} draft {state['best']['draft']}",
              flush=True)
    print("best:", json.dumps(state["best"]), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
