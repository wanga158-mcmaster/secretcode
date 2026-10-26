"""Joint economy/risk knob search for the main bot (cross-entropy method over config.h knobs, D361).

  python -B -X utf8 bots/main/tools/train/knobcem.py --run NAME [--gens N] [--pop P] [--elite E]
         [--maps pool,train] [--sample K] [--speed-sample S] [--lam L]

Each generation samples P knob vectors from N(mean, sd) in normalised [0, 1] space (generation 0 includes
the current defaults), builds each as main/main + overrides, and plays every candidate on the same games:
K maps x both sides against each contested opponent, plus S maps x both sides against the passive starter.
Fitness = mean contested score (1 per win + 0.25 tanh(gem margin / 400); unhealthy games -0.25) +
lam x (1000 - mean rounds to win vs passive, losses 2000) / 1000. The mean moves to the elite (smoothed).
State and per-candidate records persist in .cache/main/train/runs/NAME/ (resumable).
"""
from __future__ import annotations

import argparse
import json
import math
import random
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SNAP = dict(snapshot_root=ROOT / '.cache' / 'main' / 'snap', snapshot_owner='main')  # private snapshots, no shared reuse
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
import core  # noqa: E402
import exp  # noqa: E402

# (knob, kind, low, high, default)
SPACE = [
    ('hazard_threshold', 'f', 0.18, 0.42, 0.3),
    ('recall_health_frac', 'f', 0.30, 0.65, 0.5),
    ('disengage_health_frac', 'f', 0.15, 0.45, 0.3),
    ('camp_safety_margin', 'f', 1.10, 2.20, 1.6),
    ('hp_cost_weight', 'f', 0.40, 1.80, 1.0),
    ('engage_progress', 'f', 0.15, 0.60, 0.35),
    ('travel_recall_margin', 'i', 0, 12, 4),
    ('shop_recall_gold', 'i', 120, 600, 300),
    ('shop_recall_detour', 'i', 0, 40, 20),
    ('tribute_recall_margin', 'i', 0, 12, 4),
    ('tribute_detour_tiles', 'i', 2, 14, 8),
    ('gem_value', 'f', 1.0, 2.5, 1.5),
    ('gem_value_late', 'f', 1.0, 2.5, 1.5),
    ('gold_value', 'f', 0.6, 1.5, 1.0),
    ('retarget_ratio', 'f', 1.10, 1.80, 1.35),
    ('rechoose_every', 'i', 3, 16, 8),
    ('wait_weight', 'f', 0.5, 2.0, 1.0),
    ('capture_recall_walk', 'f', 0.1, 0.8, 0.3),
    ('capture_recall_gold', 'i', 40, 200, 90),
    ('capture_recall_hp', 'f', 0.4, 0.8, 0.6),
    ('shop_recall_scale', 'f', 0.5, 1.8, 1.0),
    ('homeward_recall_frac', 'f', 0.5, 0.9, 0.7),
    ('siege_max_trips', 'i', 1, 8, 4),
]
HX1 = ('anchors/main-v53+hunt_mode=1+hunt_radius=20+hunt_health=0.4f+fight_better_trades_only=false'
       '+limited_chase=false@.cache/main/train/sparring/hx1.txt')


def norm(k: int, v: float) -> float:
    _, _, lo, hi, _ = SPACE[k]
    return (v - lo) / (hi - lo)


def knob_value(k: int, u: float) -> str:
    name, kind, lo, hi, _ = SPACE[k]
    v = lo + min(1.0, max(0.0, u)) * (hi - lo)
    return str(int(round(v))) if kind == 'i' else f'{v:.5f}f'


def overrides(u: list[float]) -> dict[str, str]:
    return {SPACE[k][0]: knob_value(k, x) for k, x in enumerate(u)}


def contested(row: dict) -> float:
    a = 0 if row['a_side'] == 'A' else 1
    if row['exhausted'][a] or row['faults'][a] or row['errors'][a] or row.get('error'):
        return -0.25
    margin = row['gems'][a] - row['gems'][1 - a]
    return (1.0 if row['winner'] == 'a' else 0.0) + 0.25 * math.tanh(margin / 400.0)


def rounds(row: dict) -> float:
    a = 0 if row['a_side'] == 'A' else 1
    if row['exhausted'][a] or row['faults'][a] or row['errors'][a] or row.get('error'):
        return 2500.0
    return row['rounds'] if row['winner'] == 'a' else 2000.0


def opponent(sel: str) -> tuple[str, str]:
    base, _, weights = sel.partition('@')
    name, *kv = base.split('+')
    over = dict(x.split('=', 1) for x in kv)
    if weights:
        over.setdefault('learned', '1')
    e = core.prepare(core.bot(name), over, **SNAP)
    if not e.ok:
        sys.exit(f'build of {sel} failed:\n{e.log[-1500:]}')
    return exp.wasm(e), (str(ROOT / weights) if weights else '-')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--run', required=True)
    p.add_argument('--gens', type=int, default=10)
    p.add_argument('--pop', type=int, default=20)
    p.add_argument('--elite', type=int, default=5)
    p.add_argument('--maps', default='pool,train')
    p.add_argument('--sample', type=int, default=30, help='maps per generation vs contested opponents')
    p.add_argument('--speed-sample', type=int, default=12, help='maps per generation vs the passive starter')
    p.add_argument('--lam', type=float, default=1.0)
    p.add_argument('--sd0', type=float, default=0.18)
    p.add_argument('--vs', default=f'anchors/main-v53,{HX1}')
    p.add_argument('--seed', type=int, default=1)
    a = p.parse_args()

    run = ROOT / '.cache/main/train/runs' / a.run
    run.mkdir(parents=True, exist_ok=True)
    maps = [str(core.map_path(m)) for m in core.map_set(a.maps)]
    if any('generated/heldout' in m.replace('\\', '/') or 'generated/fresh' in m.replace('\\', '/') for m in maps):
        sys.exit('heldout/fresh maps are for claims only')
    state_path = run / 'state.json'
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding='utf-8'))
    else:
        state = {'gen': 0, 'mean': [norm(k, s[4]) for k, s in enumerate(SPACE)], 'sd': [a.sd0] * len(SPACE),
                 'config': vars(a), 'best': None}
    contents = core.read_closure(core.bot('main/main'))  # pin the source once
    opps = {f'o{j}': opponent(s) for j, s in enumerate(s for s in a.vs.split(',') if s)}
    passive = opponent('anchors/starter-cpp')
    log = run / 'log.jsonl'
    for _ in range(a.gens):
        g = state['gen']
        t0 = time.time()
        rng = random.Random(a.seed * 100003 + g)
        pop = []
        if g == 0:
            pop.append(list(state['mean']))
        while len(pop) < a.pop:
            pop.append([min(1.0, max(0.0, rng.gauss(m, s))) for m, s in zip(state['mean'], state['sd'])])
        items = [(core.bot('main/main'), overrides(u), f'c{i}', contents) for i, u in enumerate(pop)]
        built = core.prepare_many(items, **SNAP)
        bad = [i for i, e in enumerate(built) if not e.ok]
        if bad:
            sys.exit(f'candidate builds failed: {bad}\n{built[bad[0]].log[-1500:]}')
        bots = {f'c{i}': exp.wasm(e) for i, e in enumerate(built)}
        bots.update({k: v[0] for k, v in opps.items()})
        bots['ps'] = passive[0]
        played = rng.sample(maps, min(a.sample, len(maps)))
        sped = rng.sample(maps, min(a.speed_sample, len(maps)))
        lines = []
        for i in range(len(pop)):
            for o, (_, w) in opps.items():
                lines += [f'{m} {side} c{i} - {o} {w} c{i}.{o}' for m in played for side in 'AB']
            lines += [f'{m} {side} c{i} - ps - c{i}.ps' for m in sped for side in 'AB']
        tmp = run / 'tmp'
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir()
        rows = exp.run_games(lines, bots, tmp / 'run')
        shutil.rmtree(tmp, ignore_errors=True)
        con: dict[int, list[float]] = {i: [] for i in range(len(pop))}
        spd: dict[int, list[float]] = {i: [] for i in range(len(pop))}
        unhealthy = [0] * len(pop)
        for r in rows:
            cand, opp = r['tag'].split('.')
            i = int(cand[1:])
            side = 0 if r['a_side'] == 'A' else 1
            unhealthy[i] += bool(r['exhausted'][side] or r['faults'][side] or r['errors'][side] or r.get('error'))
            (spd if opp == 'ps' else con)[i].append(rounds(r) if opp == 'ps' else contested(r))
        recs = []
        for i, u in enumerate(pop):
            c = sum(con[i]) / max(1, len(con[i]))
            s = sum(spd[i]) / max(1, len(spd[i]))
            recs.append({'i': i, 'fit': c + a.lam * (1000.0 - s) / 1000.0, 'contested': round(c, 4),
                         'rounds': round(s, 1), 'unhealthy': unhealthy[i], 'u': [round(x, 4) for x in u],
                         'knobs': overrides(u)})
        recs.sort(key=lambda r: -r['fit'])
        elite = [pop[r['i']] for r in recs[:a.elite]]
        for k in range(len(SPACE)):
            em = sum(e[k] for e in elite) / len(elite)
            es = math.sqrt(sum((e[k] - em) ** 2 for e in elite) / len(elite))
            state['mean'][k] = 0.5 * em + 0.5 * state['mean'][k]
            state['sd'][k] = max(0.07, 0.5 * es + 0.5 * state['sd'][k])
        state['gen'] = g + 1
        state['best'] = recs[0]
        with log.open('a', encoding='utf-8') as f:
            f.write(json.dumps({'gen': g, 'minutes': round((time.time() - t0) / 60, 2), 'games': len(rows),
                                'cands': recs, 'mean': overrides(state['mean'])}) + '\n')
        state_path.write_text(json.dumps(state), encoding='utf-8')
        base = next((r for r in recs if g == 0 and r['i'] == 0), None)
        print(f"gen {g}: best {recs[0]['fit']:+.4f} (con {recs[0]['contested']:.3f}, rounds {recs[0]['rounds']:.0f}) "
              f"median {recs[len(recs) // 2]['fit']:+.4f}"
              + (f" defaults {base['fit']:+.4f} (con {base['contested']:.3f}, rounds {base['rounds']:.0f})" if base else '')
              + f" unhealthy {sum(unhealthy)} ({len(rows)} games, {(time.time() - t0) / 60:.1f} min)", flush=True)


if __name__ == '__main__':
    main()
