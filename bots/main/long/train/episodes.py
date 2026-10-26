"""Farm-episode data engine (S1): plays a subject with datalog=1 against opponents with bundles, keeps only the
subject's "X" episode lines (Brain::log_episode) as jsonl, deletes the bundles chunk by chunk.

  python -B -X utf8 bots/main/long/train/episodes.py collect --out NAME [--knobs K=V,...] [--maps train]
         [--vs anchors/main-v44] [--chunk 120]
  python -B -X utf8 bots/main/long/train/episodes.py calib --data NAME [--by type|need|camp]

collect writes .cache/main/long/data/NAME.jsonl (regenerable: games are deterministic).
calib compares the forecast with the outcome: realised gems-equivalent per round of the episode
(dgems + dgold / 10) / (end - start) against the predicted score, for episodes that ended in a capture
(dgold > 0) and how often episodes were abandoned or died, grouped by champion type, team size or camp kind.
"""
from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'arena'))
import core  # noqa: E402
import exp  # noqa: E402

DATA = ROOT / '.cache/main/long/data'
FIELDS = ['start', 'end', 'group', 'gold', 'gems', 'total_hp', 'strength', 'travel', 'score', 'need', 'dgold', 'dgems',
          'hp0', 'hp1', 'max_hp', 'died', 'type', 'next', 'engaged', 'status_end', 'dist_end']
STATUS = {0: 'alive', 1: 'reward', 2: 'respawning'}
TYPES = {0: 'ryth', 1: 'vesper', 2: 'vendetta', 3: 'cassi', 4: 'issac', 5: 'stellar', 6: 'prism', 7: 'marcella',
         8: 'willow', 9: 'ren', 10: 'varg', 11: 'tavi', 12: 'heart'}


def parse(stdout: str) -> list[dict]:
    out = []
    for line in stdout.splitlines():
        p = line.split()
        if len(p) == len(FIELDS) + 2 and p[0] == 'X':
            try:
                e = {k: float(v) for k, v in zip(FIELDS, p[1:-1])}
            except ValueError:
                continue
            e['why'] = p[-1]
            out.append(e)
    return out


def build(bot: str, overrides: dict) -> str:
    e = core.prepare(core.bot(bot), overrides)
    if not e.ok:
        log = e.log if isinstance(e.log, str) else '\n'.join(e.log)
        sys.exit('build failed:\n' + '\n'.join(l for l in log.splitlines() if 'error' in l)[:3000])
    return exp.wasm(e)


def cmd_collect(a) -> None:
    knobs = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
    knobs['datalog'] = '1'
    bots = {'s': build(a.bot, knobs)}
    opps = []
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        bots[f'o{j}'] = build(sel, {})
        opps.append(f'o{j}')
    maps = [str(core.map_path(m)) for m in core.map_set(a.maps)]
    lines = [f'{m} {side} s - {o} - g.{o}' for o in opps for m in maps for side in 'AB']
    DATA.mkdir(parents=True, exist_ok=True)
    out = DATA / f'{a.out}.jsonl'
    tmp = DATA / f'{a.out}.tmp'
    n_ep = 0
    with out.open('w', encoding='utf-8') as fh:
        for c in range(0, len(lines), a.chunk):
            shutil.rmtree(tmp, ignore_errors=True)
            tmp.mkdir(parents=True)
            rows = exp.run_games(lines[c:c + a.chunk], bots, tmp / 'run', bundles=tmp / 'b')
            for r in rows:
                d = tmp / 'b' / f'{Path(r["map"]).stem}_{r["a_side"]}_{r["tag"]}'
                f = d / ('team-a.stdout' if r['a_side'] == 'A' else 'team-b.stdout')
                if not f.is_file():
                    continue
                text = f.read_bytes().replace(b'\x00', b'').decode('utf-8', 'replace')
                for e in parse(text):
                    e.update(map=Path(r['map']).stem, side=r['a_side'], opp=r['tag'], won=r['winner'] == 'a')
                    fh.write(json.dumps(e) + '\n')
                    n_ep += 1
            shutil.rmtree(tmp, ignore_errors=True)
            print(f'{min(c + a.chunk, len(lines))}/{len(lines)} games, {n_ep} episodes', flush=True)


def camp_kind(e: dict) -> str:
    s = e['strength']
    return 'harmless' if s <= 0 else ('wolf-ish' if s < 60 else ('elk-ish' if s < 120 else ('golem-ish' if s < 200 else 'big')))


def cmd_calib(a) -> None:
    eps = [json.loads(l) for l in (DATA / f'{a.data}.jsonl').read_text(encoding='utf-8').splitlines()]
    key = {'type': lambda e: TYPES.get(int(e['type']), e['type']), 'need': lambda e: int(e['need']), 'camp': camp_kind}[a.by]
    agg = defaultdict(list)
    for e in eps:
        agg[key(e)].append(e)
    print(f'{len(eps)} episodes; realised = (dgems + dgold/10) per round of the episode; captured = dgold > 0')
    ends = defaultdict(lambda: [0, 0.0, 0.0])
    for e in eps:
        k = 'died' if e['died'] else ('captured' if e['dgold'] > 0 else ('retarget' if e['next'] >= 0 else 'dropped'))
        ends[k][0] += 1
        ends[k][1] += e['end'] - e['start']
        ends[k][2] += e['dgems'] + e['dgold'] / 10
    tot = sum(v[1] for v in ends.values())
    print('  endings (share of episodes / of champion-rounds / gems-equivalent earned): ' + '  '.join(
        f'{k} {v[0] / len(eps):.2f}/{v[1] / tot:.2f}/{v[2]:.0f}' for k, v in sorted(ends.items())))
    rt = [e for e in eps if not e['died'] and e['dgold'] <= 0 and e['next'] >= 0]
    if rt:
        sub = defaultdict(lambda: [0, 0.0])
        for e in rt:
            k = f"{'engaged' if e['engaged'] else 'not-engaged'}/{STATUS.get(int(e['status_end']), '?')}/{e['why']}"
            sub[k][0] += 1
            sub[k][1] += e['end'] - e['start']
        print(f'  retargets by engaged/old group status/why at the switch (share of retarget rounds; '
              f'median length {sorted(e["end"] - e["start"] for e in rt)[len(rt) // 2]:.0f}, '
              f'distance left median {sorted(e["dist_end"] for e in rt)[len(rt) // 2]:.0f}):')
        rtot = sum(v[1] for v in sub.values())
        for k, v in sorted(sub.items(), key=lambda t: -t[1][1])[:12]:
            print(f'    {k:40s} {v[0]:6d} {v[1] / rtot:.2f}')
    print(f"{a.by:>10} {'n':>6} {'capt%':>6} {'died%':>6} {'len':>5} {'pred':>7} {'real':>7} {'real/pred':>9} {'hp-%':>5} {'corr':>5}")
    for k in sorted(agg, key=str):
        es = agg[k]
        cap = [e for e in es if e['dgold'] > 0]
        L = sum(e['end'] - e['start'] for e in es) / len(es)
        pred = sum(e['score'] for e in es) / len(es)
        real = [(e['dgems'] + e['dgold'] / 10) / max(1.0, e['end'] - e['start']) for e in es]
        rmean = sum(real) / len(real)
        hp = sum((e['hp0'] - e['hp1']) / max(1.0, e['max_hp']) for e in es if not e['died']) / max(1, sum(1 for e in es if not e['died']))
        ps = [e['score'] for e in es]
        mp, mr = sum(ps) / len(ps), rmean
        cov = sum((p - mp) * (r - mr) for p, r in zip(ps, real))
        sd = math.sqrt(sum((p - mp) ** 2 for p in ps) * sum((r - mr) ** 2 for r in real)) + 1e-12
        print(f'{str(k):>10} {len(es):6d} {100 * len(cap) / len(es):6.1f} {100 * sum(e["died"] for e in es) / len(es):6.1f} {L:5.1f} '
              f'{pred:7.3f} {rmean:7.3f} {rmean / max(1e-9, pred):9.2f} {100 * hp:5.1f} {cov / sd:5.2f}')


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('collect')
    c.add_argument('--out', required=True)
    c.add_argument('--bot', default='main/long')
    c.add_argument('--knobs', default='')
    c.add_argument('--maps', default='train')
    c.add_argument('--vs', default='anchors/main-v44')
    c.add_argument('--chunk', type=int, default=120)
    k = sub.add_parser('calib')
    k.add_argument('--data', required=True)
    k.add_argument('--by', choices=('type', 'need', 'camp'), default='type')
    a = p.parse_args()
    cmd_collect(a) if a.cmd == 'collect' else cmd_calib(a)


if __name__ == '__main__':
    main()
