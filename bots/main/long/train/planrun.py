"""S1 headroom test: does following the economy solver's opening plan beat v44's own farming on the exact engine?
Plans come from train/plan.mjs (bots/main/tools/econ scheduler, pool maps only: maps/geometry.json), are written
into the learn block `plan` per game (fastrun --games weights), and played by main/long+plan=1 against the same
opponents on the same keys as the plain subject.

  python -B -X utf8 bots/main/long/train/planrun.py --name NAME [--maps pool] [--vs anchors/main-v44]
         [--horizon 400] [--m 0] [--knobs K=V,...] [--cfg JSON]
"""
from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import core  # noqa: E402
import exp  # noqa: E402
import opt  # noqa: E402

NODE = r'C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
TYPE = {'ryth': 0, 'vesper_vale': 1, 'vendetta': 2, 'cassi': 3, 'issac': 4, 'stellar': 5, 'prism': 6, 'marcella': 7,
        'willow': 8, 'ren': 9, 'varg': 10, 'tavi': 11, 'heart_of_ruin': 12}
BASE, TASKS, STRIDE = 420, 10, 31


def vector(plan: dict) -> list[float]:
    w = [0.0] * opt.COUNT
    for k in range(5):
        w[BASE + k * STRIDE] = -1.0
        w[BASE + k * STRIDE + 1] = -1.0
    for k, (name, tasks) in enumerate(list(plan['champs'].items())[:5]):
        row = BASE + k * STRIDE
        w[row] = float(TYPE.get(name, -1))
        for j in range(TASKS):
            at = row + 1 + 3 * j
            if j < len(tasks):
                w[at], w[at + 1], w[at + 2] = float(tasks[j]['x']), float(tasks[j]['y']), float(tasks[j]['party'])
            else:
                w[at] = w[at + 1] = -1.0
                break
    return w


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--name', required=True)
    ap.add_argument('--maps', default='pool')
    ap.add_argument('--vs', default='anchors/main-v44')
    ap.add_argument('--horizon', type=int, default=400)
    ap.add_argument('--m', type=int, default=0)
    ap.add_argument('--knobs', default='')
    ap.add_argument('--cfg', default='')
    ap.add_argument('--episodes', action='store_true', help='also report farm-episode endings (needs datalog=1 in --knobs)')
    a = ap.parse_args()
    run = opt.RUNS / a.name
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    names = core.map_set(a.maps)
    geo = {n: n.replace('-', '_') for n in names}  # maps/geometry.json ids use underscores
    cmd = [NODE, str(Path(__file__).resolve().parent / 'plan.mjs'), '--maps', ','.join(geo.values()), '--horizon', str(a.horizon), '--m', str(a.m)]
    if a.cfg:
        cmd += ['--cfg', a.cfg]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if p.returncode != 0:
        sys.exit(p.stderr[-1500:])
    plans = json.loads(p.stdout)
    knobs = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
    bots = {'b': opt.build('main/long', knobs), 'p': opt.build('main/long', {**knobs, 'plan': '1'})}
    opps = []
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        bots[f'o{j}'] = opt.build(sel, {})
        opps.append(f'o{j}')
    lines = []
    for m in names:
        for side in 'AB':
            pl = plans.get(f'{geo[m]}:{side}')
            if not pl or 'error' in pl:
                continue
            wf = run / f'{m}_{side}.txt'
            opt.write_vec(wf, vector(pl))
            path = str(core.map_path(m))
            for o in opps:
                lines.append(f'{path} {side} b - {o} - b.{o}.{m}')
                lines.append(f'{path} {side} p {wf} {o} - p.{o}.{m}')
    rows = exp.run_games(lines, bots, run / 'games', **({'bundles': run / 'b'} if a.episodes else {}))
    if a.episodes:
        import episodes as E  # noqa: E402
        from collections import defaultdict
        ends = {s: defaultdict(lambda: [0, 0.0, 0.0]) for s in ('b', 'p')}
        use = {s: defaultdict(int) for s in ('b', 'p')}
        for r in rows:
            d = run / 'b' / f'{Path(r["map"]).stem}_{r["a_side"]}_{r["tag"]}'
            f = d / ('team-a.stdout' if r['a_side'] == 'A' else 'team-b.stdout')
            if not f.is_file():
                continue
            text = f.read_bytes().replace(b'\x00', b'').decode('utf-8', 'replace')
            for line in text.splitlines():  # v44's time use at r500: t r500 kK pvpN hpN tribN capN fightN stageN travN idleN awaitN
                p_ = line.split()
                if len(p_) == 12 and p_[0] == 't' and p_[1] == 'r500':
                    for item in p_[3:]:
                        name = item.rstrip('0123456789')
                        use[r['tag'][0]][name] += int(item[len(name):])
            for e in E.parse(text):
                if e['start'] > a.horizon:
                    continue
                k = 'died' if e['died'] else ('captured' if e['dgold'] > 0 else ('retarget' if e['next'] >= 0 else 'dropped'))
                v = ends[r['tag'][0]][k]
                v[0] += 1; v[1] += e['end'] - e['start']; v[2] += e['dgems'] + e['dgold'] / 10
        shutil.rmtree(run / 'b', ignore_errors=True)
        for s, name in (('b', 'base'), ('p', 'plan')):
            tot = sum(v[1] for v in ends[s].values()) or 1
            print(f'  episodes starting by r{a.horizon}, {name}: ' + '  '.join(
                f'{k} n {v[0]} rounds {v[1] / tot:.2f} gems-eq {v[2]:.0f}' for k, v in sorted(ends[s].items())))
            ut = sum(use[s].values()) or 1
            print(f'    time use to r500: ' + ' '.join(f'{k} {v / ut:.2f}' for k, v in use[s].items()))
    by = {}
    for r in rows:
        subj, o, m = r['tag'].split('.', 2)
        by.setdefault((o, m, r['a_side']), {})[subj] = r
    keys = [k for k, v in by.items() if 'b' in v and 'p' in v]

    def val(r, what):
        a_ = 0 if r['a_side'] == 'A' else 1
        if what == 'win':
            return 1.0 if r['winner'] == 'a' else (0.5 if r['winner'] == 'draw' else 0.0)
        if what == 'margin':
            return r['gems'][a_] - r['gems'][1 - a_]
        if what == 'unhealthy':
            return float(bool(r['exhausted'][a_] or r['faults'][a_] or r['errors'][a_]))
        return float(r[what][a_]) if what in r else 0.0
    missing = sum(1 for m in names for side in 'AB' if 'error' in plans.get(f'{geo[m]}:{side}', {'error': 1}))
    print(f'{a.name}: {len(keys)} paired games (plan horizon {a.horizon}, rollouts m={a.m}; {missing} map-sides without a plan)')
    for o in ['all'] + opps:
        ks = [k for k in keys if o == 'all' or k[0] == o]
        print(f'  vs {o if o == "all" else a.vs.split(",")[int(o[1:])]} ({len(ks)} games)')
        for what in ('win', 'margin', 'gems', 'kills', 'deaths', 'unhealthy'):
            d = [val(by[k]['p'], what) - val(by[k]['b'], what) for k in ks]
            mu = sum(d) / len(d)
            sd = math.sqrt(sum((x - mu) ** 2 for x in d) / max(1, len(d) - 1))
            base = sum(val(by[k]['b'], what) for k in ks) / len(ks)
            print(f'    {what:10s} base {base:9.3f}  plan - base {mu:+9.3f} +- {sd / math.sqrt(len(d)):.3f}  (t {mu / (sd / math.sqrt(len(d)) + 1e-12):+.1f})')
    shutil.rmtree(run / 'games', ignore_errors=True)


if __name__ == '__main__':
    main()
