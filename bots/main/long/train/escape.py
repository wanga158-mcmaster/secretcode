"""Which way out works: among our high-hazard contact rounds in v44's online games (hazard.py's model), death within
10 rounds by how the champion moved over the next 2 rounds (toward home, toward the nearest ally, away from the
nearest enemy, or stayed), adjusted for the hazard score by logistic regression.

  python -B -X utf8 bots/main/long/train/escape.py [--min-p 0.3]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hazard as H  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--min-p', type=float, default=0.3)
    a = ap.parse_args()
    games = []
    for idx in ('index-if44.json', 'index-oth44.json'):
        for g in json.loads((H.STUDY / idx).read_text(encoding='utf-8')):
            if (H.STUDY / 'tracks' / f"{g['key']}.json").is_file() and g['key'] not in {x[0] for x in games}:
                games.append((g['key'], g['our_team']))
    X, Y, M = [], [], []
    for key, team in games:
        t = json.loads((H.STUDY / 'tracks' / f'{key}.json').read_text(encoding='utf-8'))
        rows = samples_with_moves(t, team)
        for x, y, m in rows:
            X.append(x)
            Y.append(y)
            M.append(m)
    X, Y, M = np.array(X), np.array(Y), np.array(M)
    w = H.logistic(X, Y)
    z = X @ w
    p = 1 / (1 + np.exp(-z))
    hot = p >= a.min_p
    print(f'{len(Y)} contact rounds, {hot.sum()} with p >= {a.min_p} (death rate {Y[hot].mean():.3f}, predicted {p[hot].mean():.3f})')
    names = ['toward home', 'toward ally', 'away from enemy', 'stayed']
    for j, n in enumerate(names):
        sel = hot & (M[:, j] > 0)
        print(f'  {n:16s} n {sel.sum():6d}  death {Y[sel].mean():.3f}  predicted {p[sel].mean():.3f}  excess {Y[sel].mean() - p[sel].mean():+.3f}')
    # adjusted: logit death ~ hazard logit + move indicators (hot rounds only)
    Z = np.column_stack([np.ones(hot.sum()), z[hot], M[hot]])
    v = H.logistic(Z, Y[hot])
    print('  adjusted log-odds: ' + '  '.join(f'{n} {c:+.3f}' for n, c in zip(['bias', 'hazard'] + names, v)))


def samples_with_moves(track: dict, our_team: int):
    """hazard.samples plus, per sample, the 2-round move: [home, ally, away, stayed] as 0/1 (Chebyshev change)."""
    pos = {tr['id']: {s[0]: s[1:] for s in tr['samples']} for tr in track['tracks']}
    team = {tr['id']: tr['team'] for tr in track['tracks']}
    base = H.samples(track, our_team, 10)
    # rebuild the (round, champion) order of hazard.samples to attach moves
    spawn = {0: set(), 1: set()}
    for cid, d in pos.items():
        r = min(d)
        spawn[team[cid]].add((d[r][0], d[r][1]))
    centre = (sum(p[0] for p in spawn[our_team]) / len(spawn[our_team]), sum(p[1] for p in spawn[our_team]) / len(spawn[our_team]))
    rounds = sorted({r for d in pos.values() for r in d})
    ours = [c for c in pos if team[c] == our_team]
    theirs = [c for c in pos if team[c] != our_team]
    moves = []
    for r in rounds:
        live = {c: pos[c][r] for c in pos if r in pos[c] and pos[c][r][0] >= 0}
        for c in ours:
            if c not in live:
                continue
            p = (live[c][0], live[c][1])
            if p in spawn[our_team]:
                continue
            en = [(H.cheb(p, (live[e][0], live[e][1])), e) for e in theirs if e in live and (live[e][0], live[e][1]) not in spawn[1 - our_team]]
            if not en or min(en)[0] > 8:
                continue
            al = [(H.cheb(p, (live[x][0], live[x][1])), x) for x in ours if x != c and x in live]
            q = pos[c].get(r + 2)
            m = [0, 0, 0, 0]
            if q is not None and q[0] >= 0:
                q = (q[0], q[1])
                ne = live[min(en)[1]]
                ne = (ne[0], ne[1])
                if q == p:
                    m[3] = 1
                else:
                    m[0] = int(H.cheb(q, centre) < H.cheb(p, centre))
                    if al:
                        na = live[min(al)[1]]
                        m[1] = int(H.cheb(q, (na[0], na[1])) < H.cheb(p, (na[0], na[1])))
                    m[2] = int(H.cheb(q, ne) > H.cheb(p, ne))
            moves.append(m)
    return [(x, y, m) for (x, y), m in zip(base, moves)]


if __name__ == '__main__':
    main()
