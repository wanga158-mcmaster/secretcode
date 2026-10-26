"""Death-hazard model from v44's online games (fight-study tracks, .cache/main/study/fights): for our champions
in contact (an enemy within 8, off our spawn), P(killed by an enemy champion within H rounds | features the bot
can compute itself). Logistic regression, held-out AUC by game, and the coverage/false-alarm trade-off of a
threshold.

  python -B -X utf8 bots/main/long/train/hazard.py [--horizon 10] [--out W.json]
"""
from __future__ import annotations

import argparse
import json
import random
from multiprocessing import Pool
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
STUDY = ROOT / '.cache/main/study/fights'
LONG_TRACKS = ROOT / '.cache/main/long/tracks'
# H1 (src/hazard_model.h), for comparison on new data
H1_WEIGHTS = np.array([0.44131, -1.85129, -0.26579, -0.56072, -0.69516, 0.87246, 1.04075, 0.25555, -0.24951, -3.45719,
                       -1.52364, -0.12675, 0.38594, 0.30764, 1.65970, -0.52176, 0.58817, 1.15225, -0.09465, 0.08315])
RANGED = {1, 3, 5, 6}  # vesper, cassi, stellar, prism (cambc.h champion_type); others melee
FEATURES = ['bias', 'hp_frac', 'hp/1000', 'max_hp/2000', 'allies4', 'allies8', 'enemies4', 'enemies8', 'S4', 'S8',
            'nearest_enemy/8', 'nearest_ally/8', 'ranged_enemies5', 'spawn_dist/40', 'round/1000', 'gems/100',
            'enemy_hp_mean_frac', 'hp_lost3/max', 'enemy_max_hp/2000', 'cut_off']


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def samples(track: dict, our_team: int, horizon: int):
    pos, team, typ = {}, {}, {}
    for tr in track['tracks']:
        team[tr['id']] = tr['team']
        typ[tr['id']] = tr['type']
        pos[tr['id']] = {s[0]: s[1:] for s in tr['samples']}
    spawn = {0: set(), 1: set()}
    for cid, d in pos.items():
        r = min(d)
        spawn[team[cid]].add((d[r][0], d[r][1]))
    centre = {t: (sum(p[0] for p in s) / len(s), sum(p[1] for p in s) / len(s)) for t, s in spawn.items() if s}
    rounds = sorted({r for d in pos.values() for r in d})
    ours = [c for c in pos if team[c] == our_team]
    theirs = [c for c in pos if team[c] != our_team]
    max_hp = {c: 1.0 for c in pos}
    out = []
    for r in rounds:
        live = {c: pos[c][r] for c in pos if r in pos[c] and pos[c][r][0] >= 0}
        for c, s in live.items():
            if (r - 1) not in pos[c] or pos[c][r - 1][0] < 0:
                max_hp[c] = s[2]  # respawn: full health
            max_hp[c] = max(max_hp[c], s[2])
        for c in ours:
            if c not in live:
                continue
            me = live[c]
            p = (me[0], me[1])
            if p in spawn[our_team]:
                continue
            en = [(cheb(p, (live[e][0], live[e][1])), e) for e in theirs if e in live and (live[e][0], live[e][1]) not in spawn[1 - our_team]]
            if not en or min(en)[0] > 8:
                continue
            al = [(cheb(p, (live[a][0], live[a][1])), a) for a in ours if a != c and a in live]
            # label: dead within horizon with an enemy within 6 of the last alive tile (not a camp death)
            died = False
            for k in range(1, horizon + 1):
                q = pos[c].get(r + k)
                if q is not None and q[0] < 0:
                    last = pos[c].get(r + k - 1)
                    if last and last[0] >= 0:
                        lp = (last[0], last[1])
                        died = any(e in pos and (r + k - 1) in pos[e] and pos[e][r + k - 1][0] >= 0 and
                                   cheb(lp, (pos[e][r + k - 1][0], pos[e][r + k - 1][1])) <= 6 for e in theirs)
                    break
            hp, mh = me[2], max_hp[c]
            hp4 = hp + sum(live[a][2] for d, a in al if d <= 4)
            hp8 = hp + sum(live[a][2] for d, a in al if d <= 8)
            e4 = sum(live[e][2] for d, e in en if d <= 4)
            e8 = sum(live[e][2] for d, e in en if d <= 8)
            prev = pos[c].get(r - 3)
            lost = (prev[2] - hp) / mh if prev and prev[0] >= 0 else 0.0
            home = centre[our_team]
            cut = any(d <= 5 and cheb((live[e][0], live[e][1]), home) < cheb(p, home) for d, e in en)
            x = [1.0, hp / max(1.0, mh), hp / 1000.0, mh / 2000.0,
                 sum(1 for d, _ in al if d <= 4) / 4.0, sum(1 for d, _ in al if d <= 8) / 4.0,
                 sum(1 for d, _ in en if d <= 4) / 4.0, sum(1 for d, _ in en if d <= 8) / 4.0,
                 hp4 / (hp4 + e4 + 1.0), hp8 / (hp8 + e8 + 1.0),
                 min(en)[0] / 8.0, (min(al)[0] if al else 8) / 8.0 if not al or min(al)[0] <= 8 else 1.0,
                 sum(1 for d, e in en if d <= 5 and typ[e] in RANGED) / 4.0,
                 cheb(p, home) / 40.0, r / 1000.0, me[4] / 100.0,
                 float(np.mean([live[e][2] / max(1.0, max_hp[e]) for d, e in en if d <= 8])),
                 lost, max(max_hp[e] for _, e in en) / 2000.0, 1.0 if cut else 0.0]
            out.append((x, 1.0 if died else 0.0))
    return out


def load(path: str, team: int, horizon: int):
    s = samples(json.loads(Path(path).read_text(encoding='utf-8')), team, horizon)
    return (np.array([x for x, _ in s], dtype=np.float64).reshape(-1, len(FEATURES)), np.array([y for _, y in s]))


def logistic(X, y, l2=1.0, iters=50):
    w = np.zeros(X.shape[1])
    for _ in range(iters):
        p = 1 / (1 + np.exp(-X @ w))
        g = X.T @ (p - y) + l2 * w
        h = (X * (p * (1 - p))[:, None]).T @ X + l2 * np.eye(X.shape[1])
        w -= np.linalg.solve(h, g)
    return w


def auc(s, y):
    order = np.argsort(s)
    ranks = np.empty(len(s))
    ranks[order] = np.arange(1, len(s) + 1)
    pos = y == 1
    n1, n0 = pos.sum(), (~pos).sum()
    return float((ranks[pos].sum() - n1 * (n1 + 1) / 2) / (n1 * n0))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--horizon', type=int, default=10)
    ap.add_argument('--header', help='write the C++ model header (bots/main/long/src/hazard_model.h)')
    ap.add_argument('--all', action='store_true', help='every decoded online game (train/tracks.py), not the study subset')
    ap.add_argument('--top', type=int, default=10, help='with --all: report held-out AUC on games vs ranks <= this too')
    a = ap.parse_args()
    games = []
    if a.all:
        for g in json.loads((LONG_TRACKS / 'index.json').read_text(encoding='utf-8')):
            games.append((g['key'], g['our_team'], g['opp'], g['rank'] or 999, str(LONG_TRACKS / f"{g['key']}.json")))
    else:
        for idx in ('index-if44.json', 'index-oth44.json'):
            for g in json.loads((STUDY / idx).read_text(encoding='utf-8')):
                f = STUDY / 'tracks' / f"{g['key']}.json"
                if f.is_file() and g['key'] not in {x[0] for x in games}:
                    games.append((g['key'], g['our_team'], g['opp'], g.get('rank') or 1, str(f)))
    with Pool(12) as pool:
        data = dict(zip([g[0] for g in games], pool.starmap(load, [(g[4], g[1], a.horizon) for g in games])))
    rank = {g[0]: g[3] for g in games}
    keys = sorted(data)
    random.Random(1).shuffle(keys)
    test = set(keys[::4])
    X = lambda ks: np.vstack([data[k][0] for k in ks if len(data[k][1])])
    Y = lambda ks: np.concatenate([data[k][1] for k in ks if len(data[k][1])])
    tr, te = [k for k in keys if k not in test], [k for k in keys if k in test]
    Xtr, ytr, Xte, yte = X(tr), Y(tr), X(te), Y(te)
    print(f'{len(games)} games; contact samples train {len(ytr)} (deaths {int(ytr.sum())}), test {len(yte)} (deaths {int(yte.sum())})')
    w = logistic(Xtr, ytr)
    s = Xte @ w
    print(f'held-out AUC {auc(s, yte):.3f}; S8 alone {auc(-Xte[:, 9], yte):.3f}; S4 alone {auc(-Xte[:, 8], yte):.3f}')
    if a.all:
        top = np.concatenate([np.full(len(data[k][1]), rank[k] <= a.top) for k in te if len(data[k][1])])
        print(f'  vs ranks <= {a.top}: {top.sum()} samples, death rate {yte[top].mean():.3f}, AUC {auc(s[top], yte[top]):.3f}; '
              f'others: death rate {yte[~top].mean():.3f}, AUC {auc(s[~top], yte[~top]):.3f}')
        if H1_WEIGHTS is not None:
            s1 = Xte @ H1_WEIGHTS
            print(f'  H1 weights on the same test: AUC {auc(s1, yte):.3f}; vs ranks <= {a.top} {auc(s1[top], yte[top]):.3f}')
    early = (Xte[:, 1] >= 0.8) & (Xte[:, 17] <= 0.0)
    print(f'pre-contact rounds (hp >= 80%, not hit in 3 rounds): {early.mean():.2f} of contact rounds, death rate '
          f'{yte[early].mean():.3f}, AUC {auc(s[early], yte[early]):.3f} (S8 alone {auc(-Xte[early, 9], yte[early]):.3f}); '
          f'share of death-rounds {yte[early].sum() / yte.sum():.2f}')
    p = 1 / (1 + np.exp(-s))
    base = yte.mean()
    for h in (0.05, 0.1, 0.15, 0.2, 0.3, 0.4):
        fire = p >= h
        print(f'  threshold {h:.2f}: fires on {fire.mean():.3f} of contact rounds, catches {yte[fire].sum() / yte.sum():.2f} of '
              f'death-rounds, death rate when firing {yte[fire].mean() if fire.any() else 0:.3f} (base {base:.3f})')
    for name, v in sorted(zip(FEATURES, w), key=lambda t: -abs(t[1])):
        print(f'    {name:20s} {v:+.3f}')
    if a.header:
        wall = logistic(np.vstack([Xtr, Xte]), np.concatenate([ytr, yte]))
        lines = ['#pragma once', '// Generated by bots/main/long/train/hazard.py from v44\'s online games (fight-study tracks):',
                 f'// logistic P(killed by an enemy within {a.horizon} rounds | contact features), held-out AUC {auc(s, yte):.3f}.',
                 '// Feature order: ' + ', '.join(FEATURES) + '.',
                 'namespace mb::hazard {', f'inline constexpr int kFeatures = {len(FEATURES)};',
                 'inline constexpr float kWeights[kFeatures] = { ' + ', '.join(f'{v:.5f}f' for v in wall) + ' };',
                 '} // namespace mb::hazard', '']
        Path(a.header).write_bytes('\n'.join(lines).encode('utf-8'))  # LF sources
        print(f'wrote {a.header}')


if __name__ == '__main__':
    main()
