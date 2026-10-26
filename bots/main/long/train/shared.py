"""Capture sharing (E1 follow-up): of each team's gold-gain events up to round R, how many champions gained in the
same round (a capture reward splits among allies present), and team gold per gain-round, us vs top teams.

  python -B -X utf8 bots/main/long/train/shared.py [--round 300] [--top 5]
"""
from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from multiprocessing import Pool
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
TRACKS = ROOT / '.cache/main/long/tracks'


def events(args):
    key, path, R = args
    t = json.loads(Path(path).read_text(encoding='utf-8'))
    by = defaultdict(lambda: defaultdict(list))  # team -> round -> gains
    for tr in t['tracks']:
        prev = None
        for row in sorted(tr['samples']):
            if row[0] > R:
                break
            if prev is not None and row[1] >= 0 and prev[1] >= 0 and row[4] > prev[4]:
                by[tr['team']][row[0]].append(row[4] - prev[4])
            prev = row
    return key, {team: [(len(g), sum(g)) for g in rounds.values()] for team, rounds in by.items()}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--round', type=int, default=300)
    ap.add_argument('--top', type=int, default=5)
    a = ap.parse_args()
    idx = json.loads((TRACKS / 'index.json').read_text(encoding='utf-8'))
    with Pool(12) as pool:
        res = dict(pool.map(events, [(g['key'], str(TRACKS / f"{g['key']}.json"), a.round) for g in idx]))
    agg = defaultdict(lambda: [Counter(), 0, 0, 0])
    for g in idx:
        top = (g['rank'] or 999) <= a.top
        for team, ev in res[g['key']].items():
            who = ('us vs top' if top else 'us vs rest') if team == g['our_team'] else ('top' if top else None)
            if who is None:
                continue
            v = agg[who]
            v[3] += 1
            for n, gold in ev:
                v[0][min(n, 4)] += 1
                v[1] += gold
                v[2] += 1
    for who, (c, gold, n, games) in agg.items():
        tot = sum(c.values())
        print(f'{who:11s} games {games:5d}  gain-rounds/game {n / games:5.1f}  team gold per gain-round {gold / max(1, n):6.1f}  '
              f'champions gaining together: ' + '  '.join(f'{k}: {c[k] / tot:.2f}' for k in sorted(c)))


if __name__ == '__main__':
    main()
