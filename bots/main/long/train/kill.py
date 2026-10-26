"""Kill-chance model from our online games (train/tracks.py): for each (our champion c, enemy champion e) pair in
contact (e within 6 of c, neither on its spawn), P(e dies within H rounds near enemies of its own team, i.e. killed
by us | pair features both bots can see). Held-out AUC by game, and how often a high-chance target is one v44 was
not closing on (c's distance to e did not shrink over the next 2 rounds).

  python -B -X utf8 bots/main/long/train/kill.py [--horizon 10]
"""
from __future__ import annotations

import argparse
import json
import random
import sys
from multiprocessing import Pool
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hazard as H  # noqa: E402

FEATURES = ['bias', 'e_hp_frac', 'e_hp/1000', 'e_max/2000', 'dist/6', 'my_hp_frac', 'S8', 'eS4', 'e_allies4',
            'e_allies8', 'our4_e', 'our8_e', 'e_ranged', 'e_lost3', 'e_home/40', 'round/1000']


def pairs(track: dict, our_team: int, horizon: int):
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
                max_hp[c] = s[2]
            max_hp[c] = max(max_hp[c], s[2])
        xy = {c: (v[0], v[1]) for c, v in live.items()}
        for e in theirs:
            if e not in live or xy[e] in spawn[1 - our_team]:
                continue
            near = [(H.cheb(xy[c], xy[e]), c) for c in ours if c in live and xy[c] not in spawn[our_team]]
            near = [t for t in near if t[0] <= 6]
            if not near:
                continue
            # label: e dead within horizon with one of ours within 6 of its last tile
            died = False
            for k in range(1, horizon + 1):
                q = pos[e].get(r + k)
                if q is not None and q[0] < 0:
                    last = pos[e].get(r + k - 1)
                    if last and last[0] >= 0:
                        lp = (last[0], last[1])
                        died = any((r + k - 1) in pos[o] and pos[o][r + k - 1][0] >= 0 and
                                   H.cheb(lp, (pos[o][r + k - 1][0], pos[o][r + k - 1][1])) <= 6 for o in ours)
                    break
            ehp, emh = live[e][2], max_hp[e]
            eal = [H.cheb(xy[e], xy[o]) for o in theirs if o != e and o in live]
            our4 = sum(1 for o in ours if o in live and H.cheb(xy[o], xy[e]) <= 4)
            our8 = sum(1 for o in ours if o in live and H.cheb(xy[o], xy[e]) <= 8)
            prev = pos[e].get(r - 3)
            elost = (prev[2] - ehp) / emh if prev and prev[0] >= 0 else 0.0
            e4 = ehp + sum(live[o][2] for o in theirs if o != e and o in live and H.cheb(xy[e], xy[o]) <= 4)
            o4 = sum(live[o][2] for o in ours if o in live and H.cheb(xy[o], xy[e]) <= 4)
            for d, c in near:
                hp8 = live[c][2] + sum(live[a][2] for a in ours if a != c and a in live and H.cheb(xy[a], xy[c]) <= 8)
                en8 = sum(live[o][2] for o in theirs if o in live and H.cheb(xy[o], xy[c]) <= 8)
                q = pos[c].get(r + 2)
                closing = 1 if q is not None and q[0] >= 0 and H.cheb((q[0], q[1]), xy[e]) < d else 0
                x = [1.0, ehp / max(1.0, emh), ehp / 1000.0, emh / 2000.0, d / 6.0, live[c][2] / max(1.0, max_hp[c]),
                     hp8 / (hp8 + en8 + 1.0), e4 / (e4 + o4 + 1.0), sum(1 for v in eal if v <= 4) / 4.0,
                     sum(1 for v in eal if v <= 8) / 4.0, our4 / 4.0, our8 / 4.0, 1.0 if typ[e] in H.RANGED else 0.0,
                     elost, H.cheb(xy[e], centre[1 - our_team]) / 40.0, r / 1000.0]
                out.append((x, 1.0 if died else 0.0, closing, c, r))
    return out


def load(path: str, team: int, horizon: int):
    s = pairs(json.loads(Path(path).read_text(encoding='utf-8')), team, horizon)
    return (np.array([t[0] for t in s], dtype=np.float32).reshape(-1, len(FEATURES)), np.array([t[1] for t in s]),
            np.array([t[2] for t in s]))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--horizon', type=int, default=10)
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--header', help='write the C++ model header (bots/main/long/src/kill_model.h)')
    a = ap.parse_args()
    games = json.loads((H.LONG_TRACKS / 'index.json').read_text(encoding='utf-8'))
    if a.limit:
        games = games[:a.limit]
    with Pool(12) as pool:
        res = pool.starmap(load, [(str(H.LONG_TRACKS / f"{g['key']}.json"), g['our_team'], a.horizon) for g in games])
    data = {g['key']: d for g, d in zip(games, res)}
    rank = {g['key']: g['rank'] or 999 for g in games}
    keys = sorted(data)
    random.Random(1).shuffle(keys)
    test = set(keys[::4])
    pick = lambda ks, i: np.concatenate([data[k][i] for k in ks if len(data[k][1])]).astype(np.float64)
    tr, te = [k for k in keys if k not in test], [k for k in keys if k in test]
    Xtr, ytr, Xte, yte, cte = pick(tr, 0), pick(tr, 1), pick(te, 0), pick(te, 1), pick(te, 2)
    print(f'{len(games)} games; pairs train {len(ytr)} (kills {int(ytr.sum())}), test {len(yte)}; base {yte.mean():.3f}')
    w = H.logistic(Xtr, ytr)
    p = 1 / (1 + np.exp(-(Xte @ w)))
    print(f'held-out AUC {H.auc(Xte @ w, yte):.3f}; e_hp_frac alone {H.auc(-Xte[:, 1], yte):.3f}; eS4 alone {H.auc(-Xte[:, 7], yte):.3f}')
    top = np.concatenate([np.full(len(data[k][1]), rank[k] <= 10) for k in te if len(data[k][1])])
    print(f'  vs ranks <= 10: base {yte[top].mean():.3f} AUC {H.auc((Xte @ w)[top], yte[top]):.3f}')
    for h in (0.2, 0.3, 0.4, 0.5):
        f = p >= h
        print(f'  p >= {h}: {f.mean():.3f} of pairs, kill rate {yte[f].mean():.3f}; closing {cte[f].mean():.2f} '
              f'(kill rate closing {yte[f & (cte == 1)].mean():.3f} vs not {yte[f & (cte == 0)].mean():.3f})')
    print(f'  all pairs closing {cte.mean():.2f}')
    for n, v in sorted(zip(FEATURES, w), key=lambda t: -abs(t[1])):
        print(f'    {n:14s} {v:+.3f}')
    if a.header:
        wall = H.logistic(np.vstack([Xtr, Xte]), np.concatenate([ytr, yte]))
        lines = ['#pragma once', "// Generated by bots/main/long/train/kill.py from our online games (train/tracks.py, all ranks):",
                 f'// logistic P(enemy killed near us within {a.horizon} rounds | pair features), held-out AUC {H.auc(Xte @ w, yte):.3f}.',
                 '// Feature order: ' + ', '.join(FEATURES) + '.',
                 'namespace mb::kill {', f'inline constexpr int kFeatures = {len(FEATURES)};',
                 'inline constexpr float kWeights[kFeatures] = { ' + ', '.join(f'{v:.5f}f' for v in wall) + ' };',
                 '} // namespace mb::kill', '']
        Path(a.header).write_bytes('\n'.join(lines).encode('utf-8'))  # LF sources
        print(f'wrote {a.header}')


if __name__ == '__main__':
    main()
