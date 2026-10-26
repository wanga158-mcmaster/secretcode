"""One optimiser harness over main/long's learned-params block (learn.h), on whole games of the exact engine.

  python -B -X utf8 bots/main/long/train/opt.py run --run NAME --algo spsa|es|cma|ga --blocks B1,B2
         [--knobs learned=1,...] [--gens N] [--pop P] [--maps fresh:24|pool,train] [--vs OPP,...]
         [--objective contest|win|gems] [--step S] [--init W.txt] [--seed K]
  python -B -X utf8 bots/main/long/train/opt.py show --run NAME

Every generation plays all candidates on identical games (common keys: the same maps, both sides, every
opponent), so differences are paired. `--maps fresh:N` generates N new maps per generation (fastrun --genmap
from maps/generated/templates.txt, seed = run seed x 1000 + generation) and deletes them afterwards: tuning
never sees heldout, and no map is reused (the overfit that sank micro2/micro3/K1). Objective per game
(subject's view): contest = win + 0.5 tanh(gem margin / 400); win; gems = tanh(margin / 400). Unhealthy games
(fault, fuel exhaustion, error line) score -1.
Algorithms (theta = the chosen block entries; s = --step, in parameter units):
- spsa: Rademacher d, theta +- c_k d (c_k = s / (k+1)^0.101), gradient (f+ - f-) / (2 c_k) d, averaged over
  --pop pairs, a_k = lr / (k+1+A)^0.602 normalised by the first gradient's size.
- es:   antithetic Gaussian pairs, centred ranks, Adam (the former es.py).
- cma:  separable CMA-ES (diagonal covariance; Ros & Hansen 2008), population --pop, mean evaluated each gen.
- ga:   population --pop, elites re-evaluated on the fresh maps, tournament(3) parents, blend crossover,
        Gaussian mutation s, (mu + lambda) survival by this generation's games.
State, log and best/mean theta persist in .cache/main/long/runs/NAME/ (resumable: rerun the same command).
"""
from __future__ import annotations

import argparse
import json
import math
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
import core  # noqa: E402
import exp  # noqa: E402

COUNT = 604
BLOCKS = {'group': (0, 16), 'fight': (16, 16), 'bank': (32, 12), 'recall': (44, 12), 'draft': (56, 12), 'shop': (68, 12),
          'spawn': (80, 8), 'group2': (88, 6), 'direct': (94, 8), 'stance': (102, 294), 'micro': (396, 24), 'plan': (420, 160), 'qval': (580, 24)}
RUNS = ROOT / '.cache/main/long/runs'
TEMPLATES = ROOT / 'maps/generated/templates.txt'


# --- vectors -------------------------------------------------------------------------------------------------

def write_vec(path: Path, w: list[float]) -> None:
    path.write_text(' '.join(f'{x:.6g}' for x in w) + '\n', encoding='utf-8')


def read_vec(path: str | None) -> list[float]:
    w = [float(x) for x in Path(path).read_text(encoding='utf-8').split()] if path else []
    return (w + [0.0] * COUNT)[:COUNT]


def embed(base: list[float], idx: list[int], x: list[float]) -> list[float]:
    w = list(base)
    for i, v in zip(idx, x):
        w[i] = v
    return w


# --- evaluation ----------------------------------------------------------------------------------------------

def score(r: dict, objective: str) -> float:
    a = 0 if r['a_side'] == 'A' else 1
    if r['exhausted'][a] or r['faults'][a] or r['errors'][a] or r.get('error'):
        return -1.0
    won = 1.0 if r['winner'] == 'a' else (0.5 if r['winner'] == 'draw' else 0.0)
    margin = math.tanh((r['gems'][a] - r['gems'][1 - a]) / 400.0)
    if objective == 'win':
        return won
    if objective == 'gems':
        return margin
    return won + 0.5 * margin


class Harness:
    def __init__(self, a, run: Path):
        self.a = a
        self.run = run
        overrides = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
        self.bots = {'s': build(a.bot, overrides)}
        self.opps = []
        for j, sel in enumerate(s for s in a.vs.split(',') if s):
            self.bots[f'o{j}'] = build(sel, {})
            self.opps.append(f'o{j}')

    def maps(self, gen: int) -> tuple[list[str], Path | None]:
        spec = self.a.maps
        if spec.startswith('fresh:'):
            n = int(spec.split(':')[1])
            out = self.run / 'maps'
            shutil.rmtree(out, ignore_errors=True)
            out.mkdir(parents=True)
            seed = self.a.seed * 1000 + gen
            p = subprocess.run([str(exp.fastrun()), '--engine', str(exp.engine_wasm()), '--genmap', str(n), '--templates',
                                str(TEMPLATES), '--outdir', str(out), '--seed', str(seed)], capture_output=True, text=True)
            got = sorted(str(m) for m in out.glob('*.map26a'))
            if p.returncode != 0 or not got:
                sys.exit(f'genmap failed: {p.stderr[-800:]}')
            return got, out
        names = core.map_set(spec)
        if any('heldout' in str(core.map_path(m)).replace('\\', '/') for m in names):
            sys.exit('heldout maps are for evaluation only')
        maps = [str(core.map_path(m)) for m in names]
        rng = random.Random(self.a.seed * 7919 + gen)
        if 0 < self.a.sample < len(maps):
            maps = rng.sample(maps, self.a.sample)
        return maps, None

    def evaluate(self, cands: dict[str, list[float]], gen: int) -> tuple[dict[str, float], dict]:
        tmp = self.run / 'tmp'
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir()
        for tag, w in cands.items():
            write_vec(tmp / f'{tag}.txt', w)
        maps, gen_dir = self.maps(gen)
        lines = [f'{m} {side} s {tmp / (tag + ".txt")} {o} - {tag}.{o}' for tag in cands for o in self.opps for m in maps for side in 'AB']
        rows = exp.run_games(lines, self.bots, tmp / 'run')
        fit: dict[str, list[float]] = {t: [] for t in cands}
        unhealthy = 0
        for r in rows:
            s = score(r, self.a.objective)
            fit[r['tag'].split('.')[0]].append(s)
            unhealthy += s == -1.0
        shutil.rmtree(tmp, ignore_errors=True)
        if gen_dir:
            shutil.rmtree(gen_dir, ignore_errors=True)
        f = {t: (sum(v) / len(v) if v else -1.0) for t, v in fit.items()}
        return f, {'games': len(rows), 'unhealthy': unhealthy, 'maps': len(maps)}


def build(bot: str, overrides: dict) -> str:
    e = core.prepare(core.bot(bot), overrides)
    if not e.ok:
        sys.exit(f'build of {bot} {overrides} failed:\n{e.log[-1500:]}')
    return exp.wasm(e)


# --- algorithms: ask(state, rng) -> candidates (name -> x); tell(state, f) updates state ---------------------

def spsa_ask(st, rng, a):
    k = st['gen']
    c = a.step / (k + 1) ** 0.101
    st['d'] = [[rng.choice((-1.0, 1.0)) for _ in st['x']] for _ in range(a.pop)]
    st['c'] = c
    cands = {'ctr': st['x']}
    for j, d in enumerate(st['d']):
        cands[f'p{j}'] = [x + c * di for x, di in zip(st['x'], d)]
        cands[f'm{j}'] = [x - c * di for x, di in zip(st['x'], d)]
    return cands


def spsa_tell(st, f, a):
    k, c, n = st['gen'], st['c'], len(st['x'])
    g = [0.0] * n
    for j, d in enumerate(st['d']):
        diff = (f[f'p{j}'] - f[f'm{j}']) / (2 * c)
        for i in range(n):
            g[i] += diff * d[i] / len(st['d'])
    norm = math.sqrt(sum(v * v for v in g)) + 1e-12
    if 'g0' not in st:
        st['g0'] = norm
    big_a = 0.1 * a.gens
    ak = a.lr * a.step * (1 + big_a) ** 0.602 / (k + 1 + big_a) ** 0.602 / st['g0']
    st['x'] = [x + ak * gi for x, gi in zip(st['x'], g)]  # ascent: f is a score


def es_ask(st, rng, a):
    st['eps'] = [[rng.gauss(0.0, 1.0) for _ in st['x']] for _ in range(a.pop)]
    cands = {'ctr': st['x']}
    for j, e in enumerate(st['eps']):
        cands[f'p{j}'] = [x + a.step * v for x, v in zip(st['x'], e)]
        cands[f'm{j}'] = [x - a.step * v for x, v in zip(st['x'], e)]
    return cands


def es_tell(st, f, a):
    pert = [t for t in f if t != 'ctr']
    order = sorted(pert, key=lambda t: f[t])
    util = {t: i / (len(order) - 1) - 0.5 for i, t in enumerate(order)}
    n = len(st['x'])
    g = [sum((util[f'p{j}'] - util[f'm{j}']) * st['eps'][j][i] for j in range(a.pop)) / a.pop for i in range(n)]
    st.setdefault('m', [0.0] * n)
    st.setdefault('v', [0.0] * n)
    t = st['gen'] + 1
    for i in range(n):
        st['m'][i] = 0.9 * st['m'][i] + 0.1 * g[i]
        st['v'][i] = 0.999 * st['v'][i] + 0.001 * g[i] ** 2
        st['x'][i] += a.lr * a.step * (st['m'][i] / (1 - 0.9 ** t)) / (math.sqrt(st['v'][i] / (1 - 0.999 ** t)) + 1e-8)


def cma_ask(st, rng, a):
    n = len(st['x'])
    if 'sigma' not in st:
        st.update(sigma=a.step, diag=[1.0] * n, pc=[0.0] * n, ps=[0.0] * n)
    st['z'] = [[rng.gauss(0.0, 1.0) for _ in range(n)] for _ in range(a.pop)]
    cands = {'ctr': st['x']}
    for j, z in enumerate(st['z']):
        cands[f'c{j}'] = [x + st['sigma'] * math.sqrt(d) * zi for x, d, zi in zip(st['x'], st['diag'], z)]
    return cands


def cma_tell(st, f, a):
    n, lam = len(st['x']), a.pop
    mu = lam // 2
    wts = [math.log(mu + 0.5) - math.log(i + 1) for i in range(mu)]
    s = sum(wts)
    wts = [w / s for w in wts]
    mueff = 1.0 / sum(w * w for w in wts)
    cs = (mueff + 2) / (n + mueff + 5)
    ds = 1 + 2 * max(0.0, math.sqrt((mueff - 1) / (n + 1)) - 1) + cs
    cc = (4 + mueff / n) / (n + 4 + 2 * mueff / n)
    c1 = 2 / ((n + 1.3) ** 2 + mueff) * (n + 2) / 3  # separable learning rates (x (n+2)/3)
    cmu = min(1 - c1, 2 * (mueff - 2 + 1 / mueff) / ((n + 2) ** 2 + mueff) * (n + 2) / 3)
    chi = math.sqrt(n) * (1 - 1 / (4 * n) + 1 / (21 * n * n))
    order = sorted(range(lam), key=lambda j: -f[f'c{j}'])[:mu]
    zw = [sum(wts[r] * st['z'][j][i] for r, j in enumerate(order)) for i in range(n)]
    sq = [math.sqrt(d) for d in st['diag']]
    st['x'] = [x + st['sigma'] * q * z for x, q, z in zip(st['x'], sq, zw)]
    st['ps'] = [(1 - cs) * p + math.sqrt(cs * (2 - cs) * mueff) * z for p, z in zip(st['ps'], zw)]
    psn = math.sqrt(sum(p * p for p in st['ps']))
    hs = psn / math.sqrt(1 - (1 - cs) ** (2 * (st['gen'] + 1))) / chi < 1.4 + 2 / (n + 1)
    st['pc'] = [(1 - cc) * p + (math.sqrt(cc * (2 - cc) * mueff) * q * z if hs else 0.0) for p, q, z in zip(st['pc'], sq, zw)]
    for i in range(n):
        rank_mu = sum(wts[r] * (sq[i] * st['z'][j][i]) ** 2 for r, j in enumerate(order))
        st['diag'][i] = max(1e-8, (1 - c1 - cmu) * st['diag'][i] + c1 * st['pc'][i] ** 2 + cmu * rank_mu)
    st['sigma'] *= math.exp((cs / ds) * (psn / chi - 1))


def ga_ask(st, rng, a):
    n = len(st['x'])
    if 'pop' not in st:
        st['pop'] = [list(st['x'])] + [[x + rng.gauss(0.0, a.step) for x in st['x']] for _ in range(a.pop - 1)]
        st['fit'] = [0.0] * a.pop
        st['fresh'] = True
    cands = {f'e{j}': x for j, x in enumerate(st['pop'])}  # elites re-played on this generation's maps
    if not st.get('fresh'):
        def pick():
            js = rng.sample(range(len(st['pop'])), min(3, len(st['pop'])))
            return st['pop'][max(js, key=lambda j: st['fit'][j])]
        for j in range(a.pop):
            p, q = pick(), pick()
            child = [pi + rng.uniform(-0.25, 1.25) * (qi - pi) for pi, qi in zip(p, q)]
            child = [c + (rng.gauss(0.0, a.step) if rng.random() < max(0.1, 3.0 / n) else 0.0) for c in child]
            cands[f'k{j}'] = child
    return cands


def ga_tell(st, f, a):
    tags = sorted(f, key=lambda t: -f[t])
    pool = {t: None for t in tags}
    alls = {**{f'e{j}': x for j, x in enumerate(st['pop'])}}
    alls.update({t: st['_cands'][t] for t in st['_cands'] if t.startswith('k')})
    keep = [t for t in pool][:a.pop]
    st['pop'] = [alls[t] for t in keep]
    st['fit'] = [f[t] for t in keep]
    st['x'] = st['pop'][0]
    st['fresh'] = False


ALGOS = {'spsa': (spsa_ask, spsa_tell), 'es': (es_ask, es_tell), 'cma': (cma_ask, cma_tell), 'ga': (ga_ask, ga_tell)}


# --- driver --------------------------------------------------------------------------------------------------

def cmd_run(a) -> None:
    run = RUNS / a.run
    run.mkdir(parents=True, exist_ok=True)
    sp = run / 'state.json'
    idx = [i for b in a.blocks.split(',') for i in range(BLOCKS[b][0], BLOCKS[b][0] + BLOCKS[b][1])]
    if sp.exists():
        st = json.loads(sp.read_text(encoding='utf-8'))
        if st['idx'] != idx or st['algo'] != a.algo:
            sys.exit('state.json belongs to another block set or algorithm; use a new --run')
    else:
        base = read_vec(a.init)
        st = {'gen': 0, 'algo': a.algo, 'idx': idx, 'base': base, 'x': [base[i] for i in idx], 'args': vars(a)}
    h = Harness(a, run)
    ask, tell = ALGOS[a.algo]
    log = run / 'log.jsonl'
    for _ in range(a.gens):
        t0 = time.time()
        rng = random.Random(a.seed * 104729 + st['gen'])
        cands = ask(st, rng, a)
        st['_cands'] = cands
        f, info = h.evaluate({t: embed(st['base'], idx, x) for t, x in cands.items()}, st['gen'])
        tell(st, f, a)
        st.pop('_cands', None)
        ctr = f.get('ctr', f.get('e0'))
        rec = {'gen': st['gen'], 'minutes': round((time.time() - t0) / 60, 2), 'ctr': round(ctr, 4),
               'best': round(max(f.values()), 4), 'mean': round(sum(f.values()) / len(f), 4), **info,
               'x': [round(v, 4) for v in st['x']]}
        if a.algo == 'cma':
            rec['sigma'] = round(st['sigma'], 4)
        st['gen'] += 1
        with log.open('a', encoding='utf-8') as fh:
            fh.write(json.dumps(rec) + '\n')
        sp.write_text(json.dumps(st), encoding='utf-8')
        write_vec(run / 'theta.txt', embed(st['base'], idx, st['x']))
        print(f"gen {rec['gen']}: ctr {rec['ctr']:+.4f} best {rec['best']:+.4f} mean {rec['mean']:+.4f} "
              f"unhealthy {info['unhealthy']} ({info['games']} games on {info['maps']} maps, {rec['minutes']} min)", flush=True)


def cmd_show(a) -> None:
    for line in (RUNS / a.run / 'log.jsonl').read_text(encoding='utf-8').splitlines():
        r = json.loads(line)
        print(f"gen {r['gen']:3d} ctr {r['ctr']:+.4f} best {r['best']:+.4f} mean {r['mean']:+.4f} unhealthy {r['unhealthy']} "
              f"games {r['games']} {r['minutes']} min" + (f" sigma {r['sigma']}" if 'sigma' in r else ''))


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--run', required=True)
    r.add_argument('--algo', choices=sorted(ALGOS), default='spsa')
    r.add_argument('--bot', default='main/long')
    r.add_argument('--blocks', required=True)
    r.add_argument('--knobs', default='learned=1')
    r.add_argument('--gens', type=int, default=10)
    r.add_argument('--pop', type=int, default=8)
    r.add_argument('--maps', default='fresh:24')
    r.add_argument('--sample', type=int, default=0)
    r.add_argument('--vs', default='anchors/main-v44,anchors/main-hunter7')
    r.add_argument('--objective', choices=('contest', 'win', 'gems'), default='contest')
    r.add_argument('--step', type=float, default=0.15)
    r.add_argument('--lr', type=float, default=1.0)
    r.add_argument('--init')
    r.add_argument('--seed', type=int, default=1)
    s = sub.add_parser('show')
    s.add_argument('--run', required=True)
    a = p.parse_args()
    cmd_run(a) if a.cmd == 'run' else cmd_show(a)


if __name__ == '__main__':
    main()
