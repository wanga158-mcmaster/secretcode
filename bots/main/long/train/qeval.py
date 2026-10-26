"""S1-Q M2 test: main/long with the learned target value (cfg::qscore, weights from qfit.py --out) against plain
main/long on identical games: fresh generated maps (never tuned on), both sides, every opponent. Paired per game,
standard errors clustered by map.

  python -B -X utf8 bots/main/long/train/qeval.py --weights W.json --name NAME [--maps 80] [--seed 501]
         [--vs anchors/starter-cpp,anchors/main-v44,anchors/main-hunter7] [--knobs K=V,...]
"""
from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import opt  # noqa: E402  (sets up tools/arena on the path)
import core  # noqa: E402
import exp  # noqa: E402
import qdata  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--weights', required=True)
    ap.add_argument('--name', required=True)
    ap.add_argument('--maps', type=int, default=80)
    ap.add_argument('--seed', type=int, default=501)
    ap.add_argument('--vs', default='anchors/starter-cpp,anchors/main-v44,anchors/main-hunter7')
    ap.add_argument('--knobs', default='')
    ap.add_argument('--qscore', type=int, default=1, help='1 ridge (from --weights), 2 boosted trees (q_model.h)')
    ap.add_argument('--mapset', default='', help='named map set (heldout,legacy for claims) instead of fresh maps')
    a = ap.parse_args()
    run = opt.RUNS / a.name
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    wq = json.loads(Path(a.weights).read_text(encoding='utf-8'))['ridge']
    w = [0.0] * opt.COUNT
    base = opt.BLOCKS['qval'][0]
    for i, v in enumerate(wq):
        w[base + i] = v
    wf = run / 'q.txt'
    opt.write_vec(wf, w)
    knobs = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
    bots = {'b': opt.build('main/long', knobs), 'q': opt.build('main/long', {**knobs, 'qscore': str(a.qscore)})}
    opps = []
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        bots[f'o{j}'] = opt.build(sel, {})
        opps.append((f'o{j}', sel))
    maps = ([str(core.map_path(m)) for m in core.map_set(a.mapset)] if a.mapset else qdata.genmaps(a.maps, a.seed, run / 'maps'))
    lines = []
    for o, _ in opps:
        for m in maps:
            for side in 'AB':
                lines.append(f'{m} {side} b - {o} - b.{o}')
                lines.append(f'{m} {side} q {wf} {o} - q.{o}')
    rows = exp.run_games(lines, bots, run / 'games')
    for r in rows:
        i = 0 if r['a_side'] == 'A' else 1
        if r['exhausted'][i] or r['faults'][i] or r['errors'][i]:
            print(f"  unhealthy: {r['tag']} {Path(r['map']).stem} {r['a_side']} exhausted {r['exhausted'][i]} faults {r['faults'][i]} "
                  f"errors {r['errors'][i]}")
    by = defaultdict(dict)
    for r in rows:
        s, o = r['tag'].split('.')
        by[(o, Path(r['map']).stem, r['a_side'])][s] = r

    def val(r, what):
        i = 0 if r['a_side'] == 'A' else 1
        if what == 'win':
            return 1.0 if r['winner'] == 'a' else (0.5 if r['winner'] == 'draw' else 0.0)
        if what == 'margin':
            return float(r['gems'][i] - r['gems'][1 - i])
        if what == 'unhealthy':
            return float(bool(r['exhausted'][i] or r['faults'][i] or r['errors'][i]))
        return float(r[what][i])
    print(f'{a.name}: {sum(1 for v in by.values() if len(v) == 2)} paired games on {len(maps)} fresh maps (seed {a.seed})')
    for o, sel in [('all', 'all')] + opps:
        keys = [k for k, v in by.items() if len(v) == 2 and (o == 'all' or k[0] == o)]
        print(f'  vs {sel} ({len(keys)})')
        for what in ('win', 'margin', 'gems', 'gold', 'kills', 'deaths', 'unhealthy'):
            cl = defaultdict(list)
            for k in keys:
                cl[k[1]].append(val(by[k]['q'], what) - val(by[k]['b'], what))
            d = [x for v in cl.values() for x in v]
            mu = sum(d) / len(d)
            means = [sum(v) / len(v) for v in cl.values()]
            sem = math.sqrt(sum((m - mu) ** 2 for m in means) / max(1, len(means) - 1) / len(means))
            b = sum(val(by[k]['b'], what) for k in keys) / len(keys)
            print(f'    {what:9s} base {b:9.3f}  q - base {mu:+9.3f} +- {sem:.3f} (map-clustered, t {mu / (sem + 1e-12):+.1f})')
    shutil.rmtree(run / 'games', ignore_errors=True)
    shutil.rmtree(run / 'maps', ignore_errors=True)


if __name__ == '__main__':
    main()
