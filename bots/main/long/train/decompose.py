"""Where the online gap opens (E1): from decoded tracks (train/tracks.py), per game and team up to round R,
champion-rounds alive, deaths, gems collected (positive carried-gem steps) and gold earned (positive gold steps).
The collected-gem gap us - them splits into downtime (our rate x alive-time difference) and farm rate
((our rate - their rate) x their alive time). Reported by opponent rank bucket and against the final result.

  python -B -X utf8 bots/main/long/train/decompose.py [--rounds 150,300,500]
"""
from __future__ import annotations

import argparse
import json
from multiprocessing import Pool
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
TRACKS = ROOT / '.cache/main/long/tracks'


def team_stats(track: dict, rounds: list[int]):
    out = {}
    for tr in track['tracks']:
        s = sorted(tr['samples'])
        t = tr['team']
        for R in rounds:
            d = out.setdefault((t, R), dict(alive=0, deaths=0, gems=0, gold=0))
            prev = None
            for row in s:
                if row[0] > R:
                    break
                alive = row[1] >= 0
                d['alive'] += alive
                if prev is not None:
                    if prev[1] >= 0 and not alive:
                        d['deaths'] += 1
                    if alive and prev[1] >= 0:
                        d['gems'] += max(0, row[5] - prev[5])
                        d['gold'] += max(0, row[4] - prev[4])
                prev = row
    return out


def load(args):
    key, path, rounds = args
    try:
        return key, team_stats(json.loads(Path(path).read_text(encoding='utf-8')), rounds)
    except Exception as e:  # noqa: BLE001
        return key, None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', default='150,300,500')
    a = ap.parse_args()
    rounds = [int(x) for x in a.rounds.split(',')]
    idx = json.loads((TRACKS / 'index.json').read_text(encoding='utf-8'))
    with Pool(12) as pool:
        res = dict(pool.map(load, [(g['key'], str(TRACKS / f"{g['key']}.json"), rounds) for g in idx]))
    buckets = [(1, 5), (6, 20), (21, 60), (61, 999)]
    for R in rounds:
        print(f'== by round {R}')
        print(f"{'ranks':>8} {'n':>5} {'win%':>5} {'aliveU':>7} {'aliveT':>7} {'dthU':>5} {'dthT':>5} {'gemU':>6} {'gemT':>6} "
              f"{'rateU':>6} {'rateT':>6} {'gap':>6} {'downtm':>7} {'rate':>6} {'goldU':>6} {'goldT':>6}")
        for lo, hi in buckets:
            rows = []
            for g in idx:
                st = res.get(g['key'])
                rk = g['rank'] or 999
                if not st or not (lo <= rk <= hi) or g['result'] not in ('win', 'loss'):
                    continue
                u, t = st[(g['our_team'], R)], st[(1 - g['our_team'], R)]
                ru, rt = u['gems'] / max(1, u['alive']), t['gems'] / max(1, t['alive'])
                rows.append([g['result'] == 'win', u['alive'], t['alive'], u['deaths'], t['deaths'], u['gems'], t['gems'],
                             ru * 1000, rt * 1000, u['gems'] - t['gems'], ru * (u['alive'] - t['alive']),
                             (ru - rt) * t['alive'], u['gold'], t['gold']])
            if not rows:
                continue
            m = np.array(rows, dtype=float).mean(axis=0)
            print(f'{lo:>3}-{hi:<4} {len(rows):5d} {100 * m[0]:5.0f} {m[1]:7.0f} {m[2]:7.0f} {m[3]:5.1f} {m[4]:5.1f} {m[5]:6.0f} '
                  f'{m[6]:6.0f} {m[7]:6.1f} {m[8]:6.1f} {m[9]:6.0f} {m[10]:7.0f} {m[11]:6.0f} {m[12]:6.0f} {m[13]:6.0f}')
    # which r300 differences predict the result (standardised logistic, all ranks)
    R = 300 if 300 in rounds else rounds[0]
    X, y = [], []
    for g in idx:
        st = res.get(g['key'])
        if not st or g['result'] not in ('win', 'loss'):
            continue
        u, t = st[(g['our_team'], R)], st[(1 - g['our_team'], R)]
        X.append([u['alive'] - t['alive'], u['deaths'] - t['deaths'], u['gems'] - t['gems'], u['gold'] - t['gold']])
        y.append(1.0 if g['result'] == 'win' else 0.0)
    X, y = np.array(X, float), np.array(y)
    Z = (X - X.mean(0)) / (X.std(0) + 1e-9)
    Z = np.column_stack([np.ones(len(y)), Z])
    w = np.zeros(Z.shape[1])
    for _ in range(30):
        p = 1 / (1 + np.exp(-Z @ w))
        h = (Z * (p * (1 - p))[:, None]).T @ Z + np.eye(Z.shape[1])
        w -= np.linalg.solve(h, Z.T @ (p - y) + w)
    print(f'r{R} standardised log-odds of winning (n {len(y)}): ' +
          ', '.join(f'{n} {v:+.2f}' for n, v in zip(['bias', 'alive diff', 'deaths diff', 'gems diff', 'gold diff'], w)))
    for j, n in enumerate(['alive', 'deaths', 'gems', 'gold']):
        print(f'  {n} diff alone: AUC-ish corr with win {np.corrcoef(X[:, j], y)[0, 1]:+.2f}')


if __name__ == '__main__':
    main()
