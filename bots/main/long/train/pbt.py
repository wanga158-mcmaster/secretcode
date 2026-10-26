"""Population-based training over module genomes and learned weights of main/long (tracks/long.md).

  python -B -X utf8 bots/main/long/train/pbt.py --run NAME [--rounds N] [--members 8] [--pairs 3] [--sample 6]

Each member is a genome (knob overrides of main/long, all existing default-off modules or alternate
modes) plus its own learned-layer weights (src/learn.h, 94). Every round all members, and a fixed
reference (default genome, initial weights), play the same sampled games (pool+train maps, both sides,
every league opponent). Each member takes one antithetic ES step (Adam) on its weights; its fitness is
the mean score of all its candidates (the neighbourhood, many games) smoothed over rounds. Every
`--exploit` rounds the worst quarter copies the best quarter (genome, weights, optimiser) and mutates
one or two genes. Builds are private snapshots (.cache/main/snap) cached by content hash.
Only state.json and log.jsonl persist under .cache/main/pbt/runs/NAME/.
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

SNAP = ROOT / '.cache/main/snap'
BOT = 'main/long'
BLOCKS = [('group', 16, 0.1), ('fight', 16, 0.1), ('bank', 12, 0.1), ('recall', 12, 0.1), ('draft', 12, 0.8), ('shop', 12, 1.0),
          ('spawn', 8, 0.3), ('group2', 6, 0.1)]
COUNT = sum(b[1] for b in BLOCKS)
STEP = [s for _, n, s in BLOCKS for _ in range(n)]

# Genes: knob -> allowed values; the first value is main's default.
GENES: dict[str, list[str]] = {
    'hunt_mode': ['0', '2', '3'],
    'tactics_s': ['0', '1'],
    'squad_size': ['1', '2'],
    'opening_party': ['0', '3'],
    'phantom_mode': ['0', '1'],
    'retreat_mode': ['0', '1'],
    'sos_radius': ['0', '8'],
    'comeback_gap': ['0', '150'],
    'standoff_ban_after': ['0', '3'],
    'standoff_recall_turns': ['0', '3'],
    'recall_tail': ['0', '1'],
    'recall_cancel_reach': ['0', '2'],
    'hazard_threshold': ['0.5f', '0.3f'],
    'shop_plan': ['7', '14'],
    'lock_mode': ['2', '3'],
    'retreat_preserve_move': ['false', 'true'],
    'retreat_retaliation_guard': ['false', 'true'],
    'pvp_camp_basic': ['false', 'true'],
    'live_commitments': ['false', 'true'],
    'melee_focus_step': ['false', 'true'],
    'camp_priority_step': ['false', 'true'],
    'threat_focus': ['false', 'true'],
    'skirmish_memo': ['false', 'true'],
    'doomed_kill': ['false', 'true'],
    'capture_hold': ['false', 'true'],
    'carrier_capture': ['false', 'true'],
    'share_sightings': ['false', 'true'],
    'focus_builds': ['false', 'true'],
    'observe_ally_channels': ['false', 'true'],
}

# Killer search (tracks/long.md): composition, grouping and pursuit modules of a top-team-like opponent.
KILLER: dict[str, list[str]] = {
    'draft_mode': ['8', '2', '3', '1'],
    'squad_size': ['1', '2', '9'],
    'squad_until_round': ['0', '400', '2000'],
    'escort_until': ['0', '300'],
    'opening_party': ['0', '3', '5'],
    'hunt_mode': ['0', '1', '2', '3'],
    'hunt_radius': ['15', '25'],
    'hunt_health': ['0.6f', '0.4f'],
    'fight_better_trades_only': ['true', 'false'],
    'fight_margin': ['0.5f', '0.0f', '1.0f'],
    'limited_chase': ['true', 'false'],
    'chase_turns': ['3', '6'],
    'focus_builds': ['false', 'true'],
    'threat_focus': ['false', 'true'],
    'doomed_kill': ['false', 'true'],
    'phantom_mode': ['0', '1'],
    'sos_radius': ['0', '8'],
    'shop_plan': ['7', '14'],
}
GENE_SETS = {'main': GENES, 'killer': KILLER}

LEAGUE = ['anchors/main-v53', 'anchors/main-v53+hunt_mode=1+hunt_radius=20+hunt_health=0.4f+fight_better_trades_only=false'
          '+limited_chase=false@.cache/main/train/sparring/hx1.txt', 'main/gank', 'main/ball', 'anchors/main-hunter7',
          'anchors/fundamentals']


def write_weights(path: Path, w: list[float]) -> None:
    path.write_text(' '.join(f'{x:.9g}' for x in w) + '\n', encoding='utf-8')


def unhealthy(row: dict, a: int) -> bool:
    return bool(row['exhausted'][a] or row['faults'][a] or row['errors'][a] or row.get('error'))


OBJECTIVE = 'win'


def score(row: dict) -> float:
    a = 0 if row['a_side'] == 'A' else 1
    if unhealthy(row, a):
        return -0.25
    margin = row['gems'][a] - row['gems'][1 - a]
    if OBJECTIVE == 'lethal':  # a win plus kills without deaths
        kd = row['kills'][a] - row['deaths'][a]
        return (1.0 if row['winner'] == 'a' else 0.0) + 0.25 * math.tanh(margin / 400.0) + 0.25 * math.tanh(kd / 6.0)
    return (1.0 if row['winner'] == 'a' else 0.0) + 0.25 * math.tanh(margin / 400.0)


def build(name: str, overrides: dict) -> str | None:
    e = core.prepare(core.bot(name), overrides, snapshot_root=SNAP, snapshot_owner='main')
    if not e.ok:
        print(f'build failed {name} {overrides}: {e.log[-600:]}', flush=True)
        return None
    return exp.wasm(e)


def opponent(sel: str) -> tuple[str, str]:
    base, _, weights = sel.partition('@')
    name, *kv = base.split('+')
    over = dict(x.split('=', 1) for x in kv)
    if weights:
        over.setdefault('learned', '1')
    w = build(name, over)
    if w is None:
        sys.exit(f'opponent {sel} does not build')
    return w, (str(ROOT / weights) if weights else '-')


def gkey(genome: dict) -> str:
    return ','.join(f'{k}={genome[k]}' for k in sorted(genome) if genome[k] != GENES[k][0])


def mutate(genome: dict, rng: random.Random, n: int) -> dict:
    g = dict(genome)
    for k in rng.sample(sorted(GENES), n):
        g[k] = rng.choice([v for v in GENES[k] if v != g[k]])
    return g


def main() -> None:
    global GENES, OBJECTIVE
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--run', required=True)
    p.add_argument('--rounds', type=int, default=10)
    p.add_argument('--members', type=int, default=8)
    p.add_argument('--pairs', type=int, default=3)
    p.add_argument('--sample', type=int, default=6, help='maps per round')
    p.add_argument('--maps', default='pool,train')
    p.add_argument('--exploit', type=int, default=3, help='rounds between exploit/explore steps')
    p.add_argument('--init', default=str(ROOT / '.cache/main/pbt/v53.txt'))
    p.add_argument('--lr', type=float, default=0.3)
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--genes', choices=sorted(GENE_SETS), default='main')
    p.add_argument('--vs', default=','.join(LEAGUE), help='league opponent selectors SEL[+K=V...][@W]')
    p.add_argument('--objective', choices=['win', 'lethal'], default='win')
    a = p.parse_args()

    run = ROOT / '.cache/main/pbt/runs' / a.run
    run.mkdir(parents=True, exist_ok=True)
    maps = [str(core.map_path(m)) for m in core.map_set(a.maps)]
    if any('generated/heldout' in m.replace('\\', '/') for m in maps):
        sys.exit('heldout maps are for evaluation only')
    state_path = run / 'state.json'
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding='utf-8'))
    else:
        GENES = GENE_SETS[a.genes]
        theta0 = [float(x) for x in Path(a.init).read_text(encoding='utf-8').split()]
        assert len(theta0) == COUNT
        rng = random.Random(a.seed)
        default = {k: v[0] for k, v in GENES.items()}
        members = []
        for i in range(a.members):
            g = default if i == 0 else mutate(default, rng, 3)
            members.append({'id': i, 'genome': g, 'theta': list(theta0), 'm': [0.0] * COUNT, 'v': [0.0] * COUNT,
                            'step': 0, 'ema': None, 'born': 0, 'parent': None})
        state = {'round': 0, 'members': members, 'theta0': theta0, 'config': vars(a), 'next_id': a.members,
                 'opponents': [x for x in a.vs.split(',') if x]}
    builds = state.setdefault('builds', {})
    GENES = GENE_SETS[state['config'].get('genes', 'main')]
    OBJECTIVE = state['config'].get('objective', 'win')

    built = {f'o{j}': opponent(sel) for j, sel in enumerate(state['opponents'])}
    opps = {k: v[0] for k, v in built.items()}
    opp_params = {k: v[1] for k, v in built.items()}
    log = run / 'log.jsonl'
    cfg = state['config']
    for _ in range(a.rounds):
        r = state['round']
        rng = random.Random(cfg['seed'] * 100003 + r)
        t0 = time.time()
        members = state['members']
        # Builds (new genomes only); a genome that fails to build is re-mutated from the default.
        need = {gkey(m['genome']): m['genome'] for m in members if gkey(m['genome']) not in builds or not Path(builds[gkey(m['genome'])]).is_file()}
        if not Path(builds.get('', '') or 'missing').is_file():
            need[''] = {k: v[0] for k, v in GENES.items()}
        for key, g in need.items():
            w = build(BOT, {'learned': '1', **{k: v for k, v in g.items() if v != GENES[k][0]}})
            builds[key] = w or ''
        for m in members:
            while not builds.get(gkey(m['genome'])):
                m['genome'] = mutate({k: v[0] for k, v in GENES.items()}, rng, 2)
                key = gkey(m['genome'])
                if key not in builds:
                    builds[key] = build(BOT, {'learned': '1', **{k: v for k, v in m['genome'].items() if v != GENES[k][0]}}) or ''
        tmp = run / 'tmp'
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir()
        bots = dict(opps)
        bkey = {}
        for key, w in builds.items():
            if w:
                bkey[key] = f'b{len(bkey)}'
                bots[bkey[key]] = w
        cands = {'ref': ('', state['theta0'])}
        eps = {}
        for m in members:
            i = m['id']
            e = [[rng.gauss(0.0, 1.0) for _ in range(COUNT)] for _ in range(cfg['pairs'])]
            eps[i] = e
            cands[f'c{i}'] = (gkey(m['genome']), m['theta'])
            for k, x in enumerate(e):
                cands[f'p{i}_{k}'] = (gkey(m['genome']), [t + s * y for t, s, y in zip(m['theta'], STEP, x)])
                cands[f'm{i}_{k}'] = (gkey(m['genome']), [t - s * y for t, s, y in zip(m['theta'], STEP, x)])
        for tag, (_, w) in cands.items():
            write_weights(tmp / f'{tag}.txt', w)
        played = rng.sample(maps, cfg['sample'])
        lines = [f'{mp} {side} {bkey[gk]} {tmp / (tag + ".txt")} {o} {opp_params[o]} {tag}.{o}'
                 for tag, (gk, _) in cands.items() for o in opps for mp in played for side in 'AB']
        rows = exp.run_games(lines, bots, tmp / 'run')
        shutil.rmtree(tmp, ignore_errors=True)
        sc: dict[str, list[float]] = {t: [] for t in cands}
        per_opp: dict[str, dict[str, list[float]]] = {t: {} for t in cands}
        bad: dict[str, int] = {t: 0 for t in cands}
        for row in rows:
            tag, o = row['tag'].split('.')
            sc[tag].append(score(row))
            per_opp[tag].setdefault(o, []).append(score(row))
            bad[tag] += unhealthy(row, 0 if row['a_side'] == 'A' else 1)
        fit = {t: (sum(v) / len(v) if v else -1.0) for t, v in sc.items()}
        rec_members = []
        for m in members:
            i = m['id']
            tags = [f'c{i}'] + [f'{s}{i}_{k}' for k in range(cfg['pairs']) for s in 'pm']
            hood = sum(fit[t] for t in tags) / len(tags)
            m['ema'] = hood if m['ema'] is None else 0.5 * m['ema'] + 0.5 * hood
            pert = tags[1:]
            order = sorted(pert, key=lambda t: fit[t])
            util = {t: j / (len(order) - 1) - 0.5 for j, t in enumerate(order)}
            grad = [0.0] * COUNT
            for k, x in enumerate(eps[i]):
                d = util[f'p{i}_{k}'] - util[f'm{i}_{k}']
                for j in range(COUNT):
                    grad[j] += d * x[j] / cfg['pairs']
            m['step'] += 1
            b1, b2, st = 0.9, 0.999, m['step']
            for j in range(COUNT):
                m['m'][j] = b1 * m['m'][j] + (1 - b1) * grad[j]
                m['v'][j] = b2 * m['v'][j] + (1 - b2) * grad[j] ** 2
                m['theta'][j] += cfg['lr'] * STEP[j] * (m['m'][j] / (1 - b1 ** st)) / (math.sqrt(m['v'][j] / (1 - b2 ** st)) + 1e-8)
            rec_members.append({'id': i, 'genome': gkey(m['genome']), 'centre': round(fit[f'c{i}'], 4), 'hood': round(hood, 4),
                                'ema': round(m['ema'], 4), 'bad': sum(bad[t] for t in tags),
                                'opp': {o: round(sum(v) / len(v), 3) for o, v in per_opp[f'c{i}'].items()}})
        events = []
        state['round'] = r + 1
        if state['round'] % cfg['exploit'] == 0:
            ranked = sorted(members, key=lambda m: m['ema'])
            q = max(1, len(members) // 4)
            for loser, winner in zip(ranked[:q], reversed(ranked[-q:])):
                old = gkey(loser['genome'])
                loser.update({'genome': mutate(winner['genome'], rng, rng.choice([1, 2])), 'theta': list(winner['theta']),
                              'm': list(winner['m']), 'v': list(winner['v']), 'step': winner['step'], 'ema': winner['ema'],
                              'born': state['round'], 'parent': winner['id']})
                events.append({'replaced': loser['id'], 'was': old, 'from': winner['id'], 'now': gkey(loser['genome'])})
        rec = {'round': r, 'minutes': round((time.time() - t0) / 60, 2), 'games': len(rows), 'maps': [Path(x).stem for x in played],
               'ref': round(fit['ref'], 4), 'ref_bad': bad['ref'], 'members': rec_members, 'events': events}
        with log.open('a', encoding='utf-8') as f:
            f.write(json.dumps(rec) + '\n')
        state_path.write_text(json.dumps(state), encoding='utf-8')
        best = max(members, key=lambda m: m['ema'])
        write_weights(run / 'best.txt', best['theta'])
        (run / 'best.json').write_text(json.dumps({'id': best['id'], 'genome': gkey(best['genome']), 'ema': best['ema']}), encoding='utf-8')
        print(f"round {r}: ref {fit['ref']:+.3f} | " + ' '.join(f"{x['id']}:{x['hood']:+.3f}/{x['ema']:+.3f}{'!' if x['bad'] else ''}"
                                                          for x in rec_members) + f" ({len(rows)} games, {rec['minutes']} min)", flush=True)
        for ev in events:
            print(f"  member {ev['replaced']} [{ev['was']}] <- {ev['from']} as [{ev['now']}]", flush=True)


if __name__ == '__main__':
    main()
