"""Self-play / league reinforcement learning for main/zero's legacy network (DESIGN.md 3.2), in the exact engine.

  python -B -X utf8 bots/main/zero/tools/rl.py --run NAME [--algo ppo|es] [--iters N] [--games 128]
         [--vs anchors/starter-cpp[,anchors/main-v53,snap,...]] [--init W.txt] [--eval-every 2] ...

PPO (default): every iteration plays --games games (pool+train maps sampled, both sides, opponents split
evenly) with temperature --temp and decision logging on (config[1] = --log-rate); each game gets its own seed
(config[2]). The subject's stdout (fastrun --bundles, deleted right after parsing) gives per champion the
sampled decisions `Z head round roster n chosen logp state phi`. Every decision of a game gets the game's
return R = win (1, draw 0.5) + 0.25 tanh(gem margin / 400) [+ annealed shaping beta * (0.5 tanh(gold /
--gold-scale) + 0.5 tanh(collected gems / 400))] - health penalty, optionally discounted per round to the game
end (--gamma). Advantage = R - V(state) (train.py's value head v.t + v0, unused by the bot, as a regression
baseline), normalised per iteration. The update is PPO-clip with Adam over train.py's numpy forward/backward;
old log-probabilities are the numpy recomputation under the behaviour weights (ratio exactly 1 at the first
epoch) and the bot-logged logp is kept as a parity check (per head |logp_bot - logp_numpy| in the log).

ES (--algo es): antithetic pairs over a subspace (--space all|heads|trunk|<head,...>), the centre and every
perturbation on the same sampled games (common random numbers), centred ranks, Adam; no logging (fast).

Opponents (--vs): bot selectors NAME[+K=V...][@WEIGHTS] (arena snapshots), `snap` = a random earlier snapshot
of this run's weights played by zero at argmax (league), `self` = the current weights at argmax.
Evaluation (--eval-every): the current weights at --eval-temp (default argmax) on a fixed set of --eval-maps
maps x both sides vs --eval-vs (default: the first --vs opponent), no logging.

Persistent (under .cache/main/zero/rl/runs/NAME/, resumable): state.npz (weights, Adam), log.jsonl,
current.txt, snaps/iterK.txt (every --snap-every iterations). Everything else is temporary and deleted.
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

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rl_data as D  # noqa: E402
import rl_run as R  # noqa: E402
import train as T  # noqa: E402

ROOT = R.ROOT
RUNS = ROOT / '.cache/main/zero/rl/runs'
PRIOR = ROOT / '.cache/main/zero/prior/prior.txt'
KC = T.KCONFIG


# ---- weights -----------------------------------------------------------------------------------------------

def read_vec(path: Path) -> np.ndarray:
    v = np.array(path.read_text(encoding='utf-8').split(), dtype=np.float32)
    if v.size != KC + T.KWEIGHTS:
        raise SystemExit(f'{path}: {v.size} floats, expected {KC + T.KWEIGHTS}')
    return v


def write_vec(path: Path, vec: np.ndarray, **cfg) -> None:
    v = vec.astype(np.float32).copy()
    for i, x in cfg.items():
        v[int(i[1:])] = x
    path.write_text(' '.join(f'{x:.9g}' for x in v) + '\n', encoding='utf-8')


def to_params(vec: np.ndarray) -> dict:
    return T.unflatten(vec[KC:].copy())


def to_vec(P: dict, base: np.ndarray) -> np.ndarray:
    flat = T.flatten(P).astype(np.float32)
    v = base.copy()
    v[KC:KC + flat.size] = flat
    return v


def legacy_names() -> list[str]:
    out = []
    for name, _ in T.shapes():
        if name == 'u_action':
            break
        out.append(name)
    return out


def space_mask(space: str) -> np.ndarray:
    """Indices (into the full 16+32768 vector) of the ES subspace."""
    names = legacy_names()
    if space == 'all':
        keep = [n for n in names if n not in ('m', 'm0', 'v', 'v0')]
    elif space == 'trunk':
        keep = ['w1', 'b1', 'w2', 'b2']
    elif space == 'heads':
        keep = [n for n in names if n.split('_', 1)[0] in ('u', 'vv', 'p', 'c') and '_' in n and not n.endswith('_draft')]
    else:
        heads = space.split(',')
        keep = [n for n in names if '_' in n and n.split('_', 1)[1] in heads and n.split('_', 1)[0] in ('u', 'vv', 'p', 'c')]
    idx, at = [], KC
    for name, shape in T.shapes():
        k = math.prod(shape)
        if name in keep:
            idx.append(np.arange(at, at + k))
        at += k
    if not idx:
        raise SystemExit(f'empty ES space {space}')
    return np.concatenate(idx)


# ---- scoring -----------------------------------------------------------------------------------------------

def side(row) -> int:
    return 0 if row['a_side'] == 'A' else 1


PENALISE_ERRORS = False  # set from cfg['penalise_errors'] (error lines are counted and logged either way)


def unhealthy(row) -> bool:
    a = side(row)
    return bool(row['exhausted'][a] or row['faults'][a] or (PENALISE_ERRORS and row['errors'][a]) or row.get('error'))


def game_return(row, cfg, beta: float) -> float:
    a = side(row)
    win = 1.0 if row['winner'] == 'a' else (0.5 if row['winner'] == 'draw' else 0.0)
    r = win + 0.25 * math.tanh((row['gems'][a] - row['gems'][1 - a]) / 400.0)
    if beta:
        gold = row.get('gold', [0, 0])[a]
        coll = row.get('collected', [0, 0])[a]
        r += beta * (0.5 * math.tanh(gold / cfg['gold_scale']) + 0.5 * math.tanh(coll / 400.0))
    if unhealthy(row):
        r -= cfg['health_penalty']
    return r


# Dense per-champion events from consecutive logged states (cand.cpp legacy state: x[1] team banked /1000,
# x[7] own deaths /10, x[12] carried gems /100, x[13] own gold /300); positive changes only for gains.
def dense_returns(rs, cfg) -> list[float]:
    order = sorted(range(len(rs)), key=lambda k: rs[k][1])
    ev = [0.0] * len(rs)
    prev = None
    for k in order:
        x = rs[k][5]
        if prev is not None:
            bank = max(0.0, (x[1] - prev[1]) * 1000.0)
            dead = max(0.0, round((x[7] - prev[7]) * 10.0))
            carry = max(0.0, (x[12] - prev[12]) * 100.0)
            gold = max(0.0, (x[13] - prev[13]) * 300.0)
            ev[k] = cfg['dense'] * (bank / 100.0 + 0.3 * carry / 100.0 + 0.2 * gold / 1000.0 - 0.5 * dead)
        prev = x
    togo = [0.0] * len(rs)
    acc, last_round = 0.0, None
    g = cfg['dense_gamma']
    for k in reversed(order):  # discounted sum of the events after each decision
        rnd = rs[k][1]
        if last_round is not None:
            acc *= g ** max(0, last_round - rnd)
        togo[k] = acc
        acc += ev[k]
        last_round = rnd
    return togo


# GAE / TD(lambda) over one champion's time-ordered decisions (--gae LAMBDA): the only reward is the game's
# own outcome at its end (lambda applies per round of game time); credit is bootstrapped from the learned value head V (train.py v.t + v0), so no
# hand-set event rewards enter. Discount --gamma per round (1 = none). Returns (advantages, value targets).
CRITIC = None  # rl_critic.Critic, loaded in main() when --gae is on


def gae_targets(rs, terminal: float, end: int, cfg, P):
    order = sorted(range(len(rs)), key=lambda k: rs[k][1])
    states = np.stack([np.asarray(rs[k][5], dtype=np.float32) for k in order])
    v = CRITIC.predict(states).astype(np.float64)
    # Until the critic has been fitted twice its values are uninformative: Monte Carlo returns (lambda 1).
    lam, g = (cfg['gae'] if CRITIC.fits >= 2 else 1.0), cfg['gamma']
    adv_o = np.zeros(len(order))
    nxt_v, nxt_round, acc = terminal, end, 0.0  # past the last decision: the outcome, received at `end`
    first = True
    for i in range(len(order) - 1, -1, -1):
        rnd = rs[order[i]][1]
        disc = g ** max(0, nxt_round - rnd) if g < 1 else 1.0
        delta = disc * nxt_v - v[i]
        acc = delta + (0.0 if first else disc * lam ** max(0, nxt_round - rnd) * acc)  # lambda per round
        first = False
        adv_o[i] = acc
        nxt_v, nxt_round = v[i], rnd
    adv = [0.0] * len(rs)
    tgt = [0.0] * len(rs)
    for i, k in enumerate(order):
        adv[k] = float(adv_o[i])
        tgt[k] = float(adv_o[i] + v[i])
    return adv, tgt


def summarise(rows) -> dict:
    if not rows:
        return {}
    out = {'games': len(rows)}
    out['win'] = round(float(np.mean([r['winner'] == 'a' for r in rows])), 4)
    out['draw'] = round(float(np.mean([r['winner'] == 'draw' for r in rows])), 4)
    for k, key in (('gems', 'gems'), ('gold', 'gold'), ('collected', 'collected'), ('kills', 'kills'), ('deaths', 'deaths')):
        if key in rows[0]:
            out[k] = round(float(np.mean([r[key][side(r)] for r in rows])), 1)
            out['opp_' + k] = round(float(np.mean([r[key][1 - side(r)] for r in rows])), 1)
    out['rounds'] = round(float(np.mean([r['rounds'] for r in rows])), 1)
    for k in ('exhausted', 'faults', 'errors'):
        out[k] = int(sum(r[k][side(r)] for r in rows))
    out['runner_errors'] = int(sum(bool(r.get('error')) for r in rows))
    out['unhealthy_games'] = int(sum(unhealthy(r) for r in rows))
    return out


# ---- opponents ----------------------------------------------------------------------------------------------

class League:
    def __init__(self, sels: list[str], zero_wasm: str, run: Path):
        self.sels, self.run = sels, run
        self.bots = {'z': zero_wasm}
        self.fixed = {}
        for j, sel in enumerate(sels):
            if sel in ('snap', 'self'):
                continue
            base, _, weights = sel.partition('@')
            name, *kv = base.split('+')
            over = dict(x.split('=', 1) for x in kv)
            key = f'o{j}'
            self.bots[key] = R.build(name, over)
            self.fixed[sel] = (key, str(ROOT / weights) if weights else '-')

    def resolve(self, sel: str, rng: random.Random, tmp: Path, current: np.ndarray, tag: str) -> tuple[str, str]:
        if sel in self.fixed:
            return self.fixed[sel]
        snaps = sorted((self.run / 'snaps').glob('iter*.txt'))
        if sel == 'snap' and snaps:
            src = read_vec(rng.choice(snaps))
        else:
            src = current
        path = tmp / f'opp-{tag}.txt'
        write_vec(path, src, c0=0.0, c1=0.0, c2=rng.random() * 1000, c10=1.0)
        return 'z', str(path)


def plan_games(maps, opps, n, rng) -> list[tuple[str, str, str]]:
    """n games as (map, side, opponent selector): opponents round-robin, maps sampled, both sides per map."""
    out = []
    per = max(1, n // (2 * len(opps)))
    for o in opps:
        for m in rng.sample(maps, min(per, len(maps))):
            out += [(m, 'A', o), (m, 'B', o)]
    return out


# ---- PPO ----------------------------------------------------------------------------------------------------

def collect(state, cfg, league, maps, it, work, rng):
    """Play one iteration's games; returns (rows, per-head record lists, parse stats)."""
    tmp = work / f'it{it}'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    vec = state['vec']
    games = plan_games(maps, cfg['vs'], cfg['games'], rng)
    lines, meta = [], {}
    for g, (m, sd, o) in enumerate(games):
        tag = f'i{it}g{g}'
        wpath = tmp / f'{tag}.txt'
        write_vec(wpath, vec, c0=cfg['temp'], c1=cfg['log_rate'], c2=(it * 4099 + g) * 0.001 + 0.0005,
                  c3=cfg['period'], c10=1.0)
        okey, ow = league.resolve(o, rng, tmp, vec, tag)
        lines.append(f'{m} {sd} z {wpath} {okey} {ow} {tag}')
        meta[tag] = o
    recs: dict[int, list] = {}
    pstat = {'lines': 0, 'games_with_logs': 0, 'cap_hits': 0}
    P_now = to_params(vec) if cfg.get('gae') else None

    def on_game(row, sa, sb):
        text = sa if row['a_side'] == 'A' else sb
        rosters = D.parse_log(text)
        row['_opp'] = meta[row['tag']]
        if not rosters:
            return
        pstat['games_with_logs'] += 1
        beta = cfg['shape'] * max(0.0, 1.0 - it / max(1, cfg['shape_iters']))
        ret = game_return(row, cfg, beta)
        end = row['rounds']
        for roster, rs in rosters.items():
            pstat['lines'] += len(rs)
            pstat['cap_hits'] += len(rs) >= 6000
            togo = dense_returns(rs, cfg) if cfg.get('dense') else None
            gae = gae_targets(rs, ret, end, cfg, P_now) if cfg.get('gae') else None
            for k, (h, rnd, n, c, lp, s, ph) in enumerate(rs):
                disc = cfg['gamma'] ** max(0, end - rnd) if cfg['gamma'] < 1 else 1.0
                r_k = ret * disc + (togo[k] if togo is not None else 0.0)
                rec = {'n': n, 'chosen': c, 'logp': lp, 'state': s, 'phi': ph, 'ret': r_k, 'game': row['tag']}
                if gae is not None:
                    rec['adv'], rec['ret'] = gae[0][k], gae[1][k]
                recs.setdefault(h, []).append(rec)

    rows = R.run_logged(lines, league.bots, tmp / 'run', on_game, chunk=cfg['chunk'])
    shutil.rmtree(tmp, ignore_errors=True)
    return rows, recs, pstat


def ppo_update(state, cfg, recs, rng_np):
    P = to_params(state['vec'])
    if cfg['head_cap']:  # a uniform subsample per head (the tail head logs ~20x more decisions than the others)
        recs = {h: ([rs[i] for i in rng_np.choice(len(rs), cfg['head_cap'], replace=False)] if len(rs) > cfg['head_cap'] else rs)
                for h, rs in recs.items()}
    batches = {h: D.Batch(h, rs) for h, rs in recs.items() if rs}
    parity, total = {}, sum(len(b.chosen) for b in batches.values())
    # Old log-probabilities under the behaviour weights (numpy), parity against the bot's own logp.
    allA = []
    for h, b in batches.items():
        lp, _ = D.policy_logp(P, b)
        dev = np.abs(lp - b.old_logp)
        parity[T.LEGACY_HEADS[h]] = {'n': int(len(dev)), 'mean': round(float(dev.mean()), 5),
                                     'p99': round(float(np.quantile(dev, .99)), 4), 'max': round(float(dev.max()), 4),
                                     'ratio': round(float(np.exp(b.old_logp - lp).mean()), 4)}
        if cfg['parity_gate'] and (dev.mean() > cfg['parity_gate'] or np.quantile(dev, .99) > 20 * cfg['parity_gate']):
            parity[T.LEGACY_HEADS[h]]['gated'] = True  # logged facts do not reproduce the bot's choice: no update
        if cfg['old_logp'] == 'numpy':
            b.old_logp = lp.astype(np.float32)
        v = D.value_of(P, b.state)
        if cfg.get('gae'):
            pass  # advantages and lambda-return targets were set per decision in collect()
        else:
            b.adv = (b.ret - v).astype(np.float32) if cfg['baseline'] else b.ret.copy()
        allA.append(b.adv)
    batches = {h: b for h, b in batches.items() if not parity[T.LEGACY_HEADS[h]].get('gated')}
    if not batches:
        return parity, {}, 0
    total = sum(len(b.chosen) for b in batches.values())
    allA = [b.adv for b in batches.values()]
    allA = np.concatenate(allA)
    mu, sd = float(allA.mean()), float(allA.std() + 1e-8)
    for b in batches.values():
        b.adv = ((b.adv - mu) / sd).astype(np.float32)
    M, V = state['M'], state['V']
    stats = []
    nmb = cfg['minibatches']
    for epoch in range(cfg['epochs']):
        perms = {h: rng_np.permutation(len(b.chosen)) for h, b in batches.items()}
        for k in range(nmb):
            G = {key: np.zeros_like(val) for key, val in P.items()}
            st = []
            for h, b in batches.items():
                p = perms[h]
                j = p[k * len(p) // nmb:(k + 1) * len(p) // nmb]
                if len(j) == 0:
                    continue
                Gh = {key: np.zeros_like(val) for key, val in P.items()}
                s = D.ppo_step_grads(P, b.take(j), Gh, clip=cfg['clip'], vcoef=cfg['vcoef'], ent=cfg['entropy'],
                                     value_trunk=cfg['value_trunk'])
                w = len(b.chosen) / total  # every decision weighs the same across heads
                for key in G:
                    G[key] += w * Gh[key]
                st.append(s)
            if cfg['l2_init']:
                for key in G:
                    G[key] += cfg['l2_init'] * (P[key] - state['P0'][key])
            gn = math.sqrt(sum(float((g * g).sum()) for g in G.values()))
            if gn > cfg['max_grad']:
                for key in G:
                    G[key] *= cfg['max_grad'] / gn
            state['t'] += 1
            T.adam_step(P, G, M, V, state['t'], cfg['lr'])
            stats.append({k2: float(np.mean([s[k2] for s in st])) for k2 in st[0]} | {'gnorm': gn})
    state['vec'] = to_vec(P, state['vec'])
    last = stats[-1]
    return parity, {k: round(v, 5) for k, v in last.items()}, total


# ---- ES -----------------------------------------------------------------------------------------------------

def es_iteration(state, cfg, league, maps, it, work, rng, rng_np):
    tmp = work / f'it{it}'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    idx = state['es_idx']
    vec = state['vec']
    eps = rng_np.standard_normal((cfg['pairs'], idx.size)).astype(np.float32)
    cands = {'c': vec}
    for k in range(cfg['pairs']):
        for s, sg in (('p', 1.0), ('m', -1.0)):
            v = vec.copy()
            v[idx] += sg * cfg['sigma'] * eps[k]
            cands[f'{s}{k}'] = v
    games = plan_games(maps, cfg['vs'], cfg['games'], rng)
    opp = {g: league.resolve(o, rng, tmp, vec, f'g{g}') for g, (_, _, o) in enumerate(games)}
    lines = []
    for tag, v in cands.items():
        write_vec(tmp / f'{tag}.txt', v, c0=cfg['temp'], c1=0.0, c2=it * 0.001 + 0.0005, c3=cfg['period'], c10=1.0)
        for g, (m, sd, o) in enumerate(games):
            lines.append(f'{m} {sd} z {tmp / (tag + ".txt")} {opp[g][0]} {opp[g][1]} {tag}.{g}')
    rows = R.run_logged(lines, league.bots, tmp / 'run', lambda *a: None, chunk=cfg['chunk'] * 4, want_stdout=False)
    shutil.rmtree(tmp, ignore_errors=True)
    sc: dict[str, list] = {t: [] for t in cands}
    by: dict[str, list] = {t: [] for t in cands}
    for row in rows:
        tag = row['tag'].split('.')[0]
        sc[tag].append(game_return(row, cfg, 0.0))
        by[tag].append(row)
    fit = {t: float(np.mean(v)) for t, v in sc.items()}
    pert = [t for t in cands if t != 'c']
    order = sorted(pert, key=lambda t: fit[t])
    util = {t: j / (len(order) - 1) - 0.5 for j, t in enumerate(order)}
    grad = np.zeros(idx.size, dtype=np.float32)
    for k in range(cfg['pairs']):
        grad += (util[f'p{k}'] - util[f'm{k}']) * eps[k] / cfg['pairs']
    # Adam ascent on the subspace (state['M']/['V'] hold the ES moments here).
    state['t'] += 1
    b1, b2, t = 0.9, 0.999, state['t']
    m, v = state['esM'], state['esV']
    m[:] = b1 * m + (1 - b1) * grad
    v[:] = b2 * v + (1 - b2) * grad * grad
    step = cfg['lr'] * (m / (1 - b1 ** t)) / (np.sqrt(v / (1 - b2 ** t)) + 1e-8)
    state['vec'][idx] += step
    return by['c'], [r for t2 in pert for r in by[t2]], {'centre': round(fit['c'], 4),
                                                       'best': round(max(fit[t2] for t2 in pert), 4),
                                                       'mean_pert': round(float(np.mean([fit[t2] for t2 in pert])), 4),
                                                       'step_rms': round(float(np.sqrt((step ** 2).mean())), 6)}


# ---- evaluation ---------------------------------------------------------------------------------------------

def evaluate(state, cfg, league, eval_maps, it, work):
    tmp = work / f'eval{it}'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    # The current weights and (paired, same build and games) the run's initial weights: zero's sources move
    # under the run, so only the paired difference attributes a change to the weights.
    subjects = {'cur': state['vec']}
    if it and cfg['eval_ref']:
        subjects['ref'] = read_vec(league.run / 'snaps' / 'iter0.txt')
    okey, ow = league.fixed[cfg['eval_vs']]
    lines = []
    for name, vec in subjects.items():
        w = tmp / f'{name}.txt'
        write_vec(w, vec, c0=cfg['eval_temp'], c1=0.0, c2=0.5, c3=cfg['period'], c10=1.0)
        lines += [f'{m} {sd} z {w} {okey} {ow} {name}.{j}' for j, m in enumerate(eval_maps) for sd in 'AB']
    rows = R.run_logged(lines, league.bots, tmp / 'run', lambda *a: None, chunk=cfg['chunk'] * 4, want_stdout=False)
    shutil.rmtree(tmp, ignore_errors=True)
    by = {name: sorted((r for r in rows if r['tag'].split('.')[0] == name), key=lambda r: (r['tag'], r['a_side']))
          for name in subjects}
    s = summarise(by['cur'])
    sc = {name: np.array([game_return(r, cfg, 0.0) for r in rs]) for name, rs in by.items()}
    s['score'] = round(float(sc['cur'].mean()), 4)
    if 'ref' in by:
        d = sc['cur'] - sc['ref']
        s['ref'] = summarise(by['ref']) | {'score': round(float(sc['ref'].mean()), 4)}
        s['delta'] = {'score': round(float(d.mean()), 4), 'se': round(float(d.std(ddof=1) / math.sqrt(len(d))), 4),
                      'win': round(s['win'] - s['ref']['win'], 4), 'gold': round(s.get('gold', 0) - s['ref'].get('gold', 0), 1),
                      'gems': round(s.get('gems', 0) - s['ref'].get('gems', 0), 1)}
    return s


# ---- main ---------------------------------------------------------------------------------------------------

def save_state(run: Path, state: dict) -> None:
    arrays = {'vec': state['vec'], 't': np.array(state['t']), 'iter': np.array(state['iter'])}
    for key in ('M', 'V', 'P0'):
        for k, v in state[key].items():
            arrays[f'{key}.{k}'] = v
    if 'esM' in state:
        arrays['esM'], arrays['esV'] = state['esM'], state['esV']
    tmp = run / 'state.tmp.npz'
    np.savez(tmp, **arrays)
    tmp.replace(run / 'state.npz')
    write_vec(run / 'current.txt', state['vec'], c0=0.0, c1=0.0)


def load_state(run: Path) -> dict:
    z = np.load(run / 'state.npz')
    st = {'vec': z['vec'], 't': int(z['t']), 'iter': int(z['iter']), 'M': {}, 'V': {}, 'P0': {}}
    for k in z.files:
        if '.' in k:
            a, b = k.split('.', 1)
            st[a][b] = z[k]
    if 'esM' in z.files:
        st['esM'], st['esV'] = z['esM'], z['esV']
    return st


DEFAULTS = dict(algo='ppo', games=128, vs='anchors/starter-cpp', maps='pool,train', temp=1.0, log_rate=0.25, period=4.0,
                lr=3e-4, epochs=3, minibatches=4, clip=0.2, vcoef=0.5, entropy=0.0, value_trunk=False, baseline=True,
                l2_init=0.0, max_grad=5.0, gamma=1.0, shape=0.0, shape_iters=20, gold_scale=3000.0, health_penalty=0.25,
                old_logp='numpy', parity_gate=0.02, head_cap=20000, pairs=8, sigma=0.02, space='heads', eval_every=2, eval_maps=24, eval_temp=0.0,
                eval_vs='', eval_ref=True, snap_every=2, chunk=96, seed=1, fuel_scale=20.0,
                penalise_errors=False, dense=0.0, dense_gamma=0.985, gae=0.0)


def show(run: Path) -> None:
    print('iter | train: win gems/opp gold exh/flt/err (games) | eval: win gems/opp gold score exh/flt/err | '
          'tail-parity goal-parity | min')
    for line in (run / 'log.jsonl').read_text(encoding='utf-8').splitlines():
        r = json.loads(line)
        if 'iter' not in r:
            continue
        tr, ev, pa = r.get('train', {}), r.get('eval', {}), r.get('parity', {})
        t = (f"{tr.get('win', '')} {tr.get('gems', '')}/{tr.get('opp_gems', '')} {tr.get('gold', '')} "
             f"{tr.get('exhausted', '')}/{tr.get('faults', '')}/{tr.get('errors', '')} ({tr.get('games', '')})") if tr else '-'
        e = (f"{ev.get('win')} {ev.get('gems')}/{ev.get('opp_gems')} {ev.get('gold')} {ev.get('score')} "
             f"{ev.get('exhausted')}/{ev.get('faults')}/{ev.get('errors')}") if ev else '-'
        p = ' '.join(str(pa.get(h, {}).get('mean', '-')) for h in ('tail', 'goal'))
        if ev.get('delta'):
            e += f" | vs init {ev['delta']}"
        print(f"{r['iter']:>4} | {t} | {e} | {p} | {r.get('minutes', '')}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--run', required=True)
    ap.add_argument('--iters', type=int, default=10, help='iterations to run in this invocation')
    ap.add_argument('--init', help=f'initial weights (default {PRIOR} if present, else train.py init)')
    for k, v in DEFAULTS.items():
        if isinstance(v, bool):
            ap.add_argument('--' + k.replace('_', '-'), type=lambda s: s.lower() in ('1', 'true', 'yes'), default=None)
        else:
            ap.add_argument('--' + k.replace('_', '-'), type=type(v), default=None)
    ap.add_argument('--show', action='store_true', help='print the run curve from log.jsonl and exit')
    a = ap.parse_args()
    run = RUNS / a.run
    if a.show:
        show(run)
        return
    run.mkdir(parents=True, exist_ok=True)
    cfg_path = run / 'config.json'
    cfg = dict(DEFAULTS)
    if cfg_path.exists():
        cfg.update(json.loads(cfg_path.read_text(encoding='utf-8')))
    for k in DEFAULTS:
        x = getattr(a, k)
        if x is not None:
            cfg[k] = x
    if isinstance(cfg['vs'], str):
        cfg['vs'] = [x for x in cfg['vs'].split(',') if x]
    cfg['eval_vs'] = cfg['eval_vs'] or next(x for x in cfg['vs'] if x not in ('snap', 'self'))
    cfg_path.write_text(json.dumps(cfg, indent=1), encoding='utf-8')

    maps = [str(R.core.map_path(m)) for m in R.core.map_set(cfg['maps'])]
    if any('heldout' in m.replace('\\', '/') or 'fresh' in m.replace('\\', '/') for m in maps):
        raise SystemExit('heldout/fresh maps are for claims only, never for training')
    eval_maps = random.Random(12345).sample(maps, min(cfg['eval_maps'], len(maps)))

    global CRITIC
    if cfg.get('gae'):
        import rl_critic
        CRITIC = rl_critic.Critic.load(run / 'critic.npz') if (run / 'critic.npz').exists() else rl_critic.Critic(seed=cfg['seed'])
    if (run / 'state.npz').exists():
        state = load_state(run)
    else:
        init = Path(a.init) if a.init else (PRIOR if PRIOR.exists() else None)
        if init:
            vec = read_vec(init)
        else:
            vec = np.zeros(KC + T.KWEIGHTS, dtype=np.float32)
            vec = to_vec(T.init(np.random.default_rng(cfg['seed'])), vec)
        P = to_params(vec)
        state = {'vec': vec, 't': 0, 'iter': 0, 'M': {k: np.zeros_like(v) for k, v in P.items()},
                 'V': {k: np.zeros_like(v) for k, v in P.items()}, 'P0': {k: v.copy() for k, v in P.items()}}
        (run / 'snaps').mkdir(exist_ok=True)
        write_vec(run / 'snaps' / 'iter0.txt', vec)
        with (run / 'log.jsonl').open('a', encoding='utf-8') as f:
            f.write(json.dumps({'start': str(init) if init else 'train.init', 'config': cfg}) + '\n')
    if cfg['algo'] == 'es':
        state['es_idx'] = space_mask(cfg['space'])
        if 'esM' not in state or state['esM'].size != state['es_idx'].size:
            state['esM'] = np.zeros(state['es_idx'].size, dtype=np.float32)
            state['esV'] = np.zeros(state['es_idx'].size, dtype=np.float32)

    global PENALISE_ERRORS
    PENALISE_ERRORS = bool(cfg['penalise_errors'])
    D.TAU = cfg['temp'] if cfg['temp'] > 0 else 1.0
    R.use_runner(cfg['fuel_scale'])
    train_hash = R.source_hash(Path(T.__file__))
    zero = R.build('main/zero')
    league = League(cfg['vs'] + ([cfg['eval_vs']] if cfg['eval_vs'] not in cfg['vs'] else []), zero, run)
    work = ROOT / '.cache/main/zero/rl/tmp' / a.run
    print(f'run {a.run}: {cfg["algo"]} from iter {state["iter"]}, zero {zero}', flush=True)

    def log(rec):
        with (run / 'log.jsonl').open('a', encoding='utf-8') as f:
            f.write(json.dumps(rec) + '\n')

    if state['iter'] == 0 and cfg['eval_every']:
        t0 = time.time()
        ev = evaluate(state, cfg, league, eval_maps, 0, work)
        log({'iter': 0, 'eval': ev, 'eval_s': round(time.time() - t0, 1)})
        print(f'iter 0 eval {ev}', flush=True)

    for _ in range(a.iters):
        it = state['iter'] + 1
        rng = random.Random(cfg['seed'] * 1000003 + it)
        rng_np = np.random.default_rng(cfg['seed'] * 1000003 + it)
        t0 = time.time()
        rec = {'iter': it}
        # Another agent edits zero's sources: rebuild (content-hashed, cached) and keep the last good build on failure.
        try:
            fresh = R.build('main/zero')
        except SystemExit as error:
            fresh = zero
            rec['build_failed'] = str(error)[-300:]
        if fresh != zero:
            if R.source_hash(Path(T.__file__)) != train_hash:
                raise SystemExit('train.py changed (network layout?): restart the run after migrating the weights')
            zero = league.bots['z'] = fresh
        rec['zero'] = Path(zero).parents[5].name  # the snapshot content hash
        if cfg['algo'] == 'ppo':
            rows, recs, pstat = collect(state, cfg, league, maps, it, work, rng)
            t1 = time.time()
            rec['play_s'] = round(t1 - t0, 1)
            rec['train'] = summarise(rows)
            rec['by_opp'] = {o: summarise([r for r in rows if r['_opp'] == o]) for o in cfg['vs']}
            rec['decisions'] = {T.LEGACY_HEADS[h]: len(v) for h, v in sorted(recs.items())}
            rec['parse'] = pstat
            if recs:
                parity, upd, total = ppo_update(state, cfg, recs, rng_np)
                rec['parity'], rec['update'] = parity, upd
                if CRITIC is not None:  # refit the training-only critic on this iteration's lambda-return targets
                    Xc = np.stack([np.asarray(r['state'], dtype=np.float32) for rs in recs.values() for r in rs])
                    yc = np.array([r['ret'] for rs in recs.values() for r in rs], dtype=np.float32)
                    pre = float(((CRITIC.predict(Xc) - yc) ** 2).mean())
                    post = CRITIC.fit(Xc, yc, epochs=4, seed=it)
                    CRITIC.save(run / 'critic.npz')
                    rec['critic'] = {'n': int(len(yc)), 'mse_before': round(pre, 4), 'mse_after': round(post, 4),
                                     'target_var': round(float(yc.var()), 4), 'fits': CRITIC.fits}
            rec['update_s'] = round(time.time() - t1, 1)
            del recs
        else:
            centre, pert, es = es_iteration(state, cfg, league, maps, it, work, rng, rng_np)
            rec['train'] = summarise(centre)
            rec['perturbed'] = summarise(pert)
            rec['es'] = es
            rec['play_s'] = round(time.time() - t0, 1)
        state['iter'] = it
        if cfg['snap_every'] and it % cfg['snap_every'] == 0:
            write_vec(run / 'snaps' / f'iter{it}.txt', state['vec'])
        save_state(run, state)
        if cfg['eval_every'] and it % cfg['eval_every'] == 0:
            t2 = time.time()
            rec['eval'] = evaluate(state, cfg, league, eval_maps, it, work)
            rec['eval_s'] = round(time.time() - t2, 1)
        rec['minutes'] = round((time.time() - t0) / 60, 2)
        log(rec)
        tr = rec['train']
        print(f"iter {it}: train win {tr.get('win')} gems {tr.get('gems')}/{tr.get('opp_gems')} gold {tr.get('gold')} "
              f"health exh {tr.get('exhausted')} flt {tr.get('faults')} err {tr.get('errors')}"
              + (f" | eval {rec['eval']}" if 'eval' in rec else '')
              + (f" | upd {rec.get('update')}" if 'update' in rec else '') + (f" | es {rec.get('es')}" if 'es' in rec else '')
              + f" ({rec['minutes']} min)", flush=True)
    shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
