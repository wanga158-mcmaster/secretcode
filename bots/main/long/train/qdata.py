"""S1-Q data engine (M1): target decisions with their candidates ("Q" lines, cfg::qlog) joined to the episode that
followed ("X" lines, cfg::datalog), from games on freshly generated maps against a set of opponents.

  python -B -X utf8 bots/main/long/train/qdata.py collect --out NAME [--maps 60] [--seed 7] [--epsilon 0.2]
         [--vs anchors/starter-cpp,anchors/main-v44,anchors/main-hunter7] [--knobs K=V,...] [--chunk 150]

Writes .cache/main/long/data/NAME.jsonl, one record per decision:
  {round, type, self, chosen, hp, max_hp, gold, gems, cands: [{g, s, need, travel, gems, gold, hp, str,
   committed, enemy, status}], out: {len, dgems, dgold, died, captured, next} | null, map, opp, side, won}
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import core  # noqa: E402
import episodes as E  # noqa: E402
import exp  # noqa: E402
import opt  # noqa: E402

CAND = ['g', 's', 'need', 'travel', 'gems', 'gold', 'hp', 'str', 'committed', 'enemy', 'status']


def parse_q(text: str) -> list[dict]:
    out = []
    for line in text.splitlines():
        if not line.startswith('Q '):
            continue
        head, *parts = line.split(' | ')
        h = head.split()
        if len(h) != 9:
            continue
        try:
            rec = dict(zip(['round', 'type', 'self', 'chosen', 'hp', 'max_hp', 'gold', 'gems'], [float(x) for x in h[1:]]))
            rec['cands'] = [dict(zip(CAND, [float(x) for x in p.split()])) for p in parts]
        except ValueError:
            continue
        if all(len(c) == len(CAND) for c in rec['cands']) and rec['cands']:
            out.append(rec)
    return out


def join(qs: list[dict], xs: list[dict]) -> list[dict]:
    by = {(int(x['start']), int(x['type'])): x for x in xs}
    for q in qs:
        x = by.get((int(q['round']), int(q['type'])))
        g = q['cands'][int(q['chosen'])]['g'] if int(q['chosen']) < len(q['cands']) else -1
        if x is None or int(x['group']) != int(g):
            q['out'] = None
            continue
        q['out'] = {'len': x['end'] - x['start'], 'dgems': x['dgems'], 'dgold': x['dgold'], 'died': x['died'],
                    'captured': 1.0 if x['dgold'] > 0 else 0.0, 'next': x['next'], 'hp_lost': x['hp0'] - x['hp1']}
    return qs


def genmaps(n: int, seed: int, out: Path) -> list[str]:
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    p = subprocess.run([str(exp.fastrun()), '--engine', str(exp.engine_wasm()), '--genmap', str(n), '--templates', str(opt.TEMPLATES),
                        '--outdir', str(out), '--seed', str(seed)], capture_output=True, text=True)
    maps = sorted(str(m) for m in out.glob('*.map26a'))
    if p.returncode != 0 or not maps:
        sys.exit(f'genmap failed: {p.stderr[-800:]}')
    return maps


def cmd_collect(a) -> None:
    knobs = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
    knobs.update(datalog='1', qlog='1', qlog_epsilon=str(a.epsilon))
    bots = {'s': opt.build(a.bot, knobs)}
    opps = []
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        bots[f'o{j}'] = opt.build(sel, {})
        opps.append((f'o{j}', sel))
    E.DATA.mkdir(parents=True, exist_ok=True)
    tmp = E.DATA / f'{a.out}.tmp'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    maps = genmaps(a.maps, a.seed, tmp / 'maps') if not a.mapset else [str(core.map_path(m)) for m in core.map_set(a.mapset)]
    wf = '-'
    if a.weights:  # a policy-iteration round: the current value model in learn block qval
        w = [0.0] * opt.COUNT
        for i, v in enumerate(json.loads(Path(a.weights).read_text(encoding='utf-8'))['ridge']):
            w[opt.BLOCKS['qval'][0] + i] = v
        wf = str(tmp / 'q.txt')
        opt.write_vec(Path(wf), w)
    lines = [f'{m} {side} s {wf} {o} - g.{o}' for o, _ in opps for m in maps for side in 'AB']
    name = dict(opps)
    n_dec = n_out = 0
    with (E.DATA / f'{a.out}.jsonl').open('w', encoding='utf-8') as fh:
        for c in range(0, len(lines), a.chunk):
            shutil.rmtree(tmp / 'b', ignore_errors=True)
            shutil.rmtree(tmp / 'run', ignore_errors=True)
            rows = exp.run_games(lines[c:c + a.chunk], bots, tmp / 'run', bundles=tmp / 'b')
            for r in rows:
                d = tmp / 'b' / f'{Path(r["map"]).stem}_{r["a_side"]}_{r["tag"]}'
                f = d / ('team-a.stdout' if r['a_side'] == 'A' else 'team-b.stdout')
                if not f.is_file():
                    continue
                text = f.read_bytes().replace(b'\x00', b'').decode('utf-8', 'replace')
                for q in join(parse_q(text), E.parse(text)):
                    q.update(map=Path(r['map']).stem, opp=name[r['tag'].split('.')[1]], side=r['a_side'], won=r['winner'] == 'a')
                    fh.write(json.dumps(q) + '\n')
                    n_dec += 1
                    n_out += q['out'] is not None
            print(f'{min(c + a.chunk, len(lines))}/{len(lines)} games, {n_dec} decisions ({n_out} joined)', flush=True)
    shutil.rmtree(tmp, ignore_errors=True)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('collect')
    c.add_argument('--out', required=True)
    c.add_argument('--bot', default='main/long')
    c.add_argument('--knobs', default='')
    c.add_argument('--maps', type=int, default=60)
    c.add_argument('--mapset', default='', help='named map set instead of fresh generated maps')
    c.add_argument('--seed', type=int, default=7)
    c.add_argument('--epsilon', type=float, default=0.2)
    c.add_argument('--vs', default='anchors/starter-cpp,anchors/main-v44,anchors/main-hunter7')
    c.add_argument('--chunk', type=int, default=150)
    c.add_argument('--weights', help='qfit.py --out JSON: play the learned value policy (add qscore=1 to --knobs)')
    a = p.parse_args()
    cmd_collect(a)


if __name__ == '__main__':
    main()
