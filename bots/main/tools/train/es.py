"""Evolution strategies over the main bot's learned weights (bots/main/main/src/learn.h, D340).

  python -B -X utf8 bots/main/tools/train/es.py --run NAME [--objective win|speed] [--gens N] [--pairs P]
         [--maps pool,train] [--sample K] [--vs OPP[,OPP...]] [--init W.txt] [--rebot]

Each generation draws P noise vectors e, plays theta + s*e, theta - s*e (s: per-block step) and the centre
theta on the same games (K maps sampled from the sets, both sides, every opponent), ranks the 2P perturbed
candidates by mean game score and moves theta along sum (u+ - u-) e with Adam. Scores: `win` is 1 per win
plus 0.25 tanh(gem margin / 400); `lethal` (sparring partners) is a win plus 0.5 tanh((kills - deaths) / 6); `speed` is -rounds/1000 to win (a loss or draw counts as 2000; use a
passive opponent). Unhealthy games (exhaustion, faults, error lines) score below a loss. Bots are arena
snapshots (main/main with learned=1, opponents by selector); games run through `arena exp`'s runner
(fastrun sharded over single-job processes holding pool slots). Only state.json, log.jsonl and theta.txt
persist under .cache/main/train/runs/NAME/ (resumable); candidate files are temporary.
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

# learn.h blocks: (name, size, step); a shorter --init or state is padded with zeros (blocks are appended)
BLOCKS = [('group', 16, 0.1), ('fight', 16, 0.1), ('bank', 12, 0.1), ('recall', 12, 0.1), ('draft', 12, 0.8), ('shop', 12, 1.0),
          ('spawn', 8, 0.3), ('group2', 6, 0.1)]
COUNT = sum(b[1] for b in BLOCKS)
STEP = [s for _, n, s in BLOCKS for _ in range(n)]


def write_weights(path: Path, w: list[float]) -> None:
    path.write_text(' '.join(f'{x:.6g}' for x in w) + '\n', encoding='utf-8')


def score(row: dict, objective: str) -> float:
    a = 0 if row['a_side'] == 'A' else 1
    won = row['winner'] == 'a'
    if row['exhausted'][a] or row['faults'][a] or row['errors'][a] or row.get('error'):  # unreleasable: the worst score
        return -2.0 if objective == 'speed' else -0.25
    if objective == 'speed':
        return -(row['rounds'] if won else 2000) / 1000.0
    margin = row['gems'][a] - row['gems'][1 - a]
    if objective == 'lethal':  # a sparring partner that kills without dying: win + 0.5 tanh((kills - deaths) / 6)
        kd = row['kills'][a] - row['deaths'][a] if 'kills' in row and 'deaths' in row else 0
        return (1.0 if won else 0.0) + 0.5 * math.tanh(kd / 6.0)
    return (1.0 if won else 0.0) + 0.25 * math.tanh(margin / 400.0)


def build(bot: str, overrides: dict | None = None) -> str:
    e = core.prepare(core.bot(bot), overrides or {}, snapshot_root=ROOT / '.cache' / 'main' / 'snap', snapshot_owner='main')
    if not e.ok:
        sys.exit(f'build of {bot} {overrides or ""} failed:\n{e.log[-1500:]}')
    return exp.wasm(e)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--run', required=True)
    p.add_argument('--objective', choices=['speed', 'win', 'lethal'], default='win')
    p.add_argument('--gens', type=int, default=10)
    p.add_argument('--pairs', type=int, default=12)
    p.add_argument('--maps', default='pool,train', help='arena map sets (pool, train, legacy, ...; never heldout)')
    p.add_argument('--sample', type=int, default=10, help='maps per generation (0: all)')
    p.add_argument('--vs', default='anchors/main-v44,anchors/main-hunter7,anchors/main-v42,anchors/main-v38',
                   help='opponent selectors (speed: anchors/starter-cpp)')
    p.add_argument('--init', help='initial weights file (default: zeros)')
    p.add_argument('--lr', type=float, default=0.3, help='Adam step in units of each block step')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--rebot', action='store_true', help='on resume, rebuild main/main (learned=1) from the working tree')
    p.add_argument('--pace-vs', default='', help='opponent selectors scored by pace, lam x (1000 - rounds to win) / 1000 (losses 2000); '
                   'the others by --objective (D361 mixed objective)')
    p.add_argument('--lam', type=float, default=1.0)
    p.add_argument('--knobs', default='', help='extra knob overrides for the trained bot, K=V[,K=V...] (kept in the run state)')
    a = p.parse_args()
    knobs = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)

    run = ROOT / '.cache/main/train/runs' / a.run
    run.mkdir(parents=True, exist_ok=True)
    maps = [str(core.map_path(m)) for m in core.map_set(a.maps)]
    if any('generated/heldout' in m.replace('\\', '/') for m in maps):
        sys.exit('heldout maps are for evaluation only; train on pool, train, legacy')
    state_path = run / 'state.json'
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding='utf-8'))
        if state['count'] > COUNT:
            sys.exit('state has more weights than the layer')
        if state['count'] < COUNT:  # the layer grew (blocks are appended): new weights start at zero
            pad = [0.0] * (COUNT - state['count'])
            for k in ('theta', 'm', 'v'):
                state[k] += pad
            print(f"resume: {state['count']} -> {COUNT} weights (appended blocks start at zero)", flush=True)
            state['count'] = COUNT
            a.rebot = True
        if a.rebot or not Path(state.get('bot', '')).is_file():
            state['bot'] = build('main/main', {'learned': '1', **state.get('knobs', {})})
    else:
        theta = [0.0] * COUNT
        if a.init:
            theta = [float(x) for x in Path(a.init).read_text(encoding='utf-8').split()]
            if len(theta) > COUNT:
                sys.exit(f'--init has {len(theta)} weights, the layer {COUNT}')
            theta += [0.0] * (COUNT - len(theta))
        state = {'count': COUNT, 'gen': 0, 'theta': theta, 'm': [0.0] * COUNT, 'v': [0.0] * COUNT,
                 'config': {k: v for k, v in vars(a).items()}, 'knobs': knobs, 'bot': build('main/main', {'learned': '1', **knobs}),
                 'opponents': [s for s in a.vs.split(',') if s]}
    # Selectors (older runs stored wasm paths, which still work while their snapshots exist).
    # An opponent may carry knob overrides and a learned weight file: SEL[+KNOB=V...][@WEIGHTS] (sparring
    # partners trained by this driver, e.g. the lethal hunter).
    def opponent(sel: str) -> tuple[str, str]:
        if sel.endswith('.wasm') and Path(sel).is_file():
            return sel, '-'
        base, _, weights = sel.partition('@')
        name, *kv = base.split('+')
        over = dict(x.split('=', 1) for x in kv)
        if weights:
            over.setdefault('learned', '1')
        return build(name, over), (str((ROOT / weights) if not Path(weights).is_absolute() else Path(weights)) if weights else '-')

    if a.pace_vs and not any(s in state['opponents'] for s in a.pace_vs.split(',')):
        state['opponents'] += [s for s in a.pace_vs.split(',') if s]
    built = {f'o{j}': opponent(sel) for j, sel in enumerate(state['opponents'])}
    pace = {f'o{j}' for j, sel in enumerate(state['opponents']) if sel in a.pace_vs.split(',')}
    opponents = {k: v[0] for k, v in built.items()}
    opp_params = {k: v[1] for k, v in built.items()}
    rng = random.Random(a.seed * 100003 + state['gen'])
    log = run / 'log.jsonl'
    for _ in range(a.gens):
        g = state['gen']
        t0 = time.time()
        tmp = run / 'tmp'
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir()
        theta = state['theta']
        eps = [[rng.gauss(0.0, 1.0) for _ in range(COUNT)] for _ in range(a.pairs)]
        cands = {'ctr': theta}
        for k, e in enumerate(eps):
            cands[f'p{k}'] = [t + s * x for t, s, x in zip(theta, STEP, e)]
            cands[f'm{k}'] = [t - s * x for t, s, x in zip(theta, STEP, e)]
        for tag, w in cands.items():
            write_weights(tmp / f'{tag}.txt', w)
        played = rng.sample(maps, a.sample) if 0 < a.sample < len(maps) else maps
        lines = [f'{m} {side} s {tmp / (tag + ".txt")} {o} {opp_params[o]} {tag}.{o}'
                 for tag in cands for o in opponents for m in played for side in 'AB']
        rows = exp.run_games(lines, {'s': state['bot'], **opponents}, tmp / 'run')
        totals: dict[str, list[float]] = {tag: [] for tag in cands}
        health = 0
        for r in rows:
            tag = r['tag'].split('.')[0]
            o = r['tag'].split('.', 1)[1]
            if o in pace:
                totals[tag].append(a.lam * (1.0 + score(r, 'speed')))  # speed score is -rounds/1000
            else:
                totals[tag].append(score(r, a.objective))
            side = 0 if r['a_side'] == 'A' else 1
            health += bool(r['exhausted'][side] or r['faults'][side] or r['errors'][side] or r.get('error'))
        shutil.rmtree(tmp, ignore_errors=True)
        fit = {tag: sum(v) / len(v) for tag, v in totals.items()}
        perturbed = [t for t in cands if t != 'ctr']
        order = sorted(perturbed, key=lambda t: fit[t])
        util = {t: i / (len(order) - 1) - 0.5 for i, t in enumerate(order)}
        grad = [0.0] * COUNT
        for k, e in enumerate(eps):
            d = util[f'p{k}'] - util[f'm{k}']
            for i in range(COUNT):
                grad[i] += d * e[i] / a.pairs
        b1, b2, step = 0.9, 0.999, g + 1
        for i in range(COUNT):
            state['m'][i] = b1 * state['m'][i] + (1 - b1) * grad[i]
            state['v'][i] = b2 * state['v'][i] + (1 - b2) * grad[i] ** 2
            mh = state['m'][i] / (1 - b1 ** step)
            vh = state['v'][i] / (1 - b2 ** step)
            theta[i] += a.lr * STEP[i] * mh / (math.sqrt(vh) + 1e-8)
        state['theta'] = theta
        state['gen'] = g + 1
        rec = {'gen': g, 'minutes': round((time.time() - t0) / 60, 2), 'centre': round(fit['ctr'], 4),
               'mean': round(sum(fit[t] for t in perturbed) / len(perturbed), 4), 'best': round(fit[order[-1]], 4),
               'worst': round(fit[order[0]], 4), 'unhealthy': health, 'games': len(rows), 'theta': [round(x, 4) for x in theta]}
        with log.open('a', encoding='utf-8') as f:
            f.write(json.dumps(rec) + '\n')
        state_path.write_text(json.dumps(state), encoding='utf-8')
        write_weights(run / 'theta.txt', theta)
        print(f"gen {g}: centre {rec['centre']:+.4f} mean {rec['mean']:+.4f} best {rec['best']:+.4f} unhealthy {health} "
              f"({rec['games']} games, {rec['minutes']} min)", flush=True)


if __name__ == '__main__':
    main()
