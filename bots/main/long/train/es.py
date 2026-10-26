"""Paired antithetic ES over chosen learned blocks of main/long (learn.h), on whole games against a league.

  python -B -X utf8 bots/main/long/train/es.py run --run NAME [--blocks micro] [--knobs learned=1,micro=1]
         [--gens N] [--pairs P] [--maps pool,train] [--sample K] [--vs OPP,...] [--step S] [--lr L] [--init W.txt]
  python -B -X utf8 bots/main/long/train/es.py vec --out W.txt [OFFSET=VALUE ...]

run: each generation draws P noise vectors over the chosen blocks, plays theta +- s*e and theta on identical
games (K sampled maps x both sides x every opponent), ranks the candidates by mean score (win + 0.25
tanh(gem margin / 400) + 0.1 tanh(kill-death margin / 5); unhealthy games -0.5) and steps theta with Adam.
State, log and theta persist in .cache/main/long/runs/NAME/; candidate files are temporary.
vec: writes a full weight vector with the given entries set (offsets into learn.h's block).
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
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
import core  # noqa: E402
import exp  # noqa: E402

COUNT = 604
BLOCKS = {'group': (0, 16), 'fight': (16, 16), 'bank': (32, 12), 'recall': (44, 12), 'draft': (56, 12), 'shop': (68, 12),
          'spawn': (80, 8), 'group2': (88, 6), 'stance': (102, 294), 'micro': (396, 24), 'plan': (420, 160), 'qval': (580, 24)}


def write(path: Path, w: list[float]) -> None:
    path.write_text(' '.join(f'{x:.6g}' for x in w) + '\n', encoding='utf-8')


def read(path: str | None) -> list[float]:
    w = [float(x) for x in Path(path).read_text(encoding='utf-8').split()] if path else []
    return (w + [0.0] * COUNT)[:COUNT]


def score(r: dict) -> float:
    a = 0 if r['a_side'] == 'A' else 1
    if r['exhausted'][a] or r['faults'][a] or r['errors'][a] or r.get('error'):
        return -0.5
    won = 1.0 if r['winner'] == 'a' else (0.5 if r['winner'] == 'draw' else 0.0)
    margin = r['gems'][a] - r['gems'][1 - a]
    kd = (r['kills'][a] - r['deaths'][a]) if 'kills' in r else 0
    return won + 0.25 * math.tanh(margin / 400.0) + 0.1 * math.tanh(kd / 5.0)


def build(bot: str, overrides: dict) -> str:
    e = core.prepare(core.bot(bot), overrides)
    if not e.ok:
        sys.exit(f'build of {bot} {overrides} failed:\n{e.log[-1500:]}')
    return exp.wasm(e)


def cmd_run(a) -> None:
    run = ROOT / '.cache/main/long/runs' / a.run
    run.mkdir(parents=True, exist_ok=True)
    idx = [i for b in a.blocks.split(',') for i in range(BLOCKS[b][0], BLOCKS[b][0] + BLOCKS[b][1])]
    maps = [str(core.map_path(m)) for m in core.map_set(a.maps)]
    if any('generated/heldout' in m.replace('\\', '/') for m in maps):
        sys.exit('heldout maps are for evaluation only')
    sp = run / 'state.json'
    if sp.exists():
        st = json.loads(sp.read_text(encoding='utf-8'))
    else:
        st = {'gen': 0, 'theta': read(a.init), 'm': [0.0] * len(idx), 'v': [0.0] * len(idx), 'idx': idx, 'args': vars(a)}
    overrides = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
    bots = {'s': build(a.bot, overrides)}
    opps = []
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        bots[f'o{j}'] = build(sel, {})
        opps.append(f'o{j}')
    rng = random.Random(a.seed * 7919 + st['gen'])
    log = run / 'log.jsonl'
    for _ in range(a.gens):
        t0 = time.time()
        tmp = run / 'tmp'
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir()
        theta = st['theta']
        eps = [[rng.gauss(0.0, 1.0) for _ in idx] for _ in range(a.pairs)]
        cands = {'ctr': list(theta)}
        for k, e in enumerate(eps):
            for sign, tag in ((1, 'p'), (-1, 'm')):
                w = list(theta)
                for i, x in zip(idx, e):
                    w[i] += sign * a.step * x
                cands[f'{tag}{k}'] = w
        for tag, w in cands.items():
            write(tmp / f'{tag}.txt', w)
        played = rng.sample(maps, a.sample) if 0 < a.sample < len(maps) else maps
        lines = [f'{m} {side} s {tmp / (tag + ".txt")} {o} - {tag}.{o}' for tag in cands for o in opps for m in played for side in 'AB']
        rows = exp.run_games(lines, bots, tmp / 'run')
        fit: dict[str, list[float]] = {t: [] for t in cands}
        unhealthy = 0
        for r in rows:
            s = score(r)
            fit[r['tag'].split('.')[0]].append(s)
            unhealthy += s == -0.5
        shutil.rmtree(tmp, ignore_errors=True)
        f = {t: sum(v) / len(v) for t, v in fit.items()}
        pert = [t for t in cands if t != 'ctr']
        order = sorted(pert, key=lambda t: f[t])
        util = {t: i / (len(order) - 1) - 0.5 for i, t in enumerate(order)}
        grad = [sum((util[f'p{k}'] - util[f'm{k}']) * eps[k][j] for k in range(a.pairs)) / a.pairs for j in range(len(idx))]
        step = st['gen'] + 1
        for j, i in enumerate(idx):
            st['m'][j] = 0.9 * st['m'][j] + 0.1 * grad[j]
            st['v'][j] = 0.999 * st['v'][j] + 0.001 * grad[j] ** 2
            theta[i] += a.lr * a.step * (st['m'][j] / (1 - 0.9 ** step)) / (math.sqrt(st['v'][j] / (1 - 0.999 ** step)) + 1e-8)
        pd = [f[f'p{k}'] - f[f'm{k}'] for k in range(a.pairs)]
        rec = {'gen': st['gen'], 'minutes': round((time.time() - t0) / 60, 2), 'centre': round(f['ctr'], 4),
               'mean': round(sum(f[t] for t in pert) / len(pert), 4), 'best': round(f[order[-1]], 4),
               'pair_spread': round(sum(abs(x) for x in pd) / len(pd), 4), 'unhealthy': unhealthy, 'games': len(rows),
               'theta': [round(theta[i], 4) for i in idx]}
        st['gen'] += 1
        st['theta'] = theta
        with log.open('a', encoding='utf-8') as fh:
            fh.write(json.dumps(rec) + '\n')
        sp.write_text(json.dumps(st), encoding='utf-8')
        write(run / 'theta.txt', theta)
        print(f"gen {rec['gen']}: centre {rec['centre']:+.4f} mean {rec['mean']:+.4f} best {rec['best']:+.4f} "
              f"pair |d| {rec['pair_spread']:.4f} unhealthy {unhealthy} ({len(rows)} games, {rec['minutes']} min)", flush=True)


def cmd_vec(a) -> None:
    w = read(a.init)
    for kv in a.set:
        k, v = kv.split('=')
        w[int(k)] = float(v)
    write(Path(a.out), w)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--run', required=True)
    r.add_argument('--bot', default='main/long')
    r.add_argument('--blocks', default='micro')
    r.add_argument('--knobs', default='learned=1,micro=1')
    r.add_argument('--gens', type=int, default=10)
    r.add_argument('--pairs', type=int, default=8)
    r.add_argument('--maps', default='pool,train')
    r.add_argument('--sample', type=int, default=12)
    r.add_argument('--vs', default='anchors/main-v44,anchors/main-hunter7,anchors/main-v45')
    r.add_argument('--step', type=float, default=0.5)
    r.add_argument('--lr', type=float, default=0.3)
    r.add_argument('--init')
    r.add_argument('--seed', type=int, default=1)
    v = sub.add_parser('vec')
    v.add_argument('--out', required=True)
    v.add_argument('--init')
    v.add_argument('set', nargs='*')
    a = p.parse_args()
    cmd_run(a) if a.cmd == 'run' else cmd_vec(a)


if __name__ == '__main__':
    main()
