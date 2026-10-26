"""Opponent-matched online comparison of two versions (bots/main, D362): for each game of version B, the expected
result is version A's win rate on the same (opponent, map, side) key, else on the same opponent. Prints matched n,
B's wins, A's expectation, the difference with a standard error, by rank band of B's opponent.

  python -B bots/main/tools/vpair.py A B
"""
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
reg = json.loads((ROOT / 'results/online/groups.json').read_text(encoding='utf-8'))
A, B = sys.argv[1], sys.argv[2]
key, opp = defaultdict(list), defaultdict(list)
games_b = []
for g in reg:
    v = str(g.get('version'))
    rank = (g.get('created') or {}).get('rank') or 0
    for r in g.get('results') or []:
        p = r.split(':')
        if len(p) < 3:
            continue
        w = 1.0 if p[2] == 'win' else 0.0
        if v == A:
            key[(g['opponent'], p[0], p[1])].append(w)
            opp[g['opponent']].append(w)
        elif v == B:
            games_b.append((g['opponent'], p[0], p[1], w, rank))
bands = [(1, 10), (11, 30), (31, 60), (61, 150), (151, 99999)]
rows = defaultdict(list)
for o, m, s, w, rank in games_b:
    base = key.get((o, m, s)) or opp.get(o)
    if not base:
        continue
    e = sum(base) / len(base)
    b = next((f'{lo}-{hi}' for lo, hi in bands if lo <= rank <= hi), '?')
    rows[b].append((w, e))
    rows['all'].append((w, e))
print(f'{B} vs {A}-expected on matched opponent keys')
for b in [f'{lo}-{hi}' for lo, hi in bands] + ['?', 'all']:
    xs = rows.get(b)
    if not xs:
        continue
    n = len(xs)
    d = [w - e for w, e in xs]
    mean = sum(d) / n
    se = math.sqrt(sum((x - mean) ** 2 for x in d) / max(1, n - 1) / n)
    print(f'{b:>10s} n {n:5d}  {B} {sum(w for w, _ in xs) / n:6.1%}  {A}-exp {sum(e for _, e in xs) / n:6.1%}  diff {mean:+.1%} +- {se:.1%}')
