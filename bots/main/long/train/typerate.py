"""Farm rate by champion type (E1 follow-up): gems and gold collected per 1000 alive rounds up to round R, for our
champions and for opponents of rank <= --top, from decoded tracks (train/tracks.py).

  python -B -X utf8 bots/main/long/train/typerate.py [--round 300] [--top 5]
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from multiprocessing import Pool
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
TRACKS = ROOT / '.cache/main/long/tracks'
TYPES = {0: 'ryth', 1: 'vesper', 2: 'vendetta', 3: 'cassi', 4: 'issac', 5: 'stellar', 6: 'prism', 7: 'marcella',
         8: 'willow', 9: 'ren', 10: 'varg', 11: 'tavi', 12: 'heart'}


def per_type(args):
    key, path, R = args
    t = json.loads(Path(path).read_text(encoding='utf-8'))
    spawn = {0: set(), 1: set()}
    for tr in t['tracks']:
        s = min(tr['samples'])
        spawn[tr['team']].add((s[1], s[2]))
    out = []
    for tr in t['tracks']:
        s = sorted(tr['samples'])
        alive = gems = gold = home = gaining = 0
        prev = None
        for row in s:
            if row[0] > R:
                break
            if row[1] >= 0:
                alive += 1
                home += (row[1], row[2]) in spawn[tr['team']]
                if prev is not None and prev[1] >= 0:
                    gems += max(0, row[5] - prev[5])
                    gold += max(0, row[4] - prev[4])
                    gaining += row[4] > prev[4]
            prev = row
        out.append((tr['team'], tr['type'], alive, gems, gold, home, gaining))
    return key, out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--round', type=int, default=300)
    ap.add_argument('--top', type=int, default=5)
    a = ap.parse_args()
    idx = json.loads((TRACKS / 'index.json').read_text(encoding='utf-8'))
    with Pool(12) as pool:
        res = dict(pool.map(per_type, [(g['key'], str(TRACKS / f"{g['key']}.json"), a.round) for g in idx]))
    agg = defaultdict(lambda: [0, 0, 0, 0, 0, 0])
    for g in idx:
        top = (g['rank'] or 999) <= a.top
        for team, typ, alive, gems, gold, home, gaining in res[g['key']]:
            if team == g['our_team']:
                k = ('us vs top' if top else 'us vs rest', typ)
            elif top:
                k = ('top', typ)
            else:
                continue
            v = agg[k]
            v[0] += 1; v[1] += alive; v[2] += gems; v[3] += gold; v[4] += home; v[5] += gaining
    print(f'up to round {a.round}; top = opponents of rank <= {a.top}; home = share of alive rounds on own spawn; '
          f'gaining = share of alive rounds with a gold gain; gold/gain = gold per gaining round')
    print(f"{'who':12s} {'type':9s} {'n':>5} {'alive':>6} {'gems/1k':>8} {'gold/1k':>8} {'home':>5} {'gaining':>7} {'gold/gain':>9}")
    for k in sorted(agg, key=lambda k: (k[0], -agg[k][0])):
        n, alive, gems, gold, home, gaining = agg[k]
        if n < 20:
            continue
        print(f'{k[0]:12s} {TYPES.get(k[1], k[1]):9s} {n:5d} {alive / n:6.0f} {1000 * gems / max(1, alive):8.1f} '
              f'{1000 * gold / max(1, alive):8.0f} {home / max(1, alive):5.2f} {gaining / max(1, alive):7.2f} {gold / max(1, gaining):9.1f}')


if __name__ == '__main__':
    main()
