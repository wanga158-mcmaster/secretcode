"""Summaries of a forks.py dataset: the chaos floor, per-option advantages, window value vs final outcome.

  python -B -X utf8 bots/main/long/train/analyse.py RUN [RUN ...]
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
NAMES = ['as_is', 'fight_close', 'fight_here', 'retreat', 'ignore', 'recall', 'noop']
DEATH = 40.0     # gems a death is worth (G2: ~40 gems plus the bag)
TERMINAL = 600.0  # a game decided inside the window


def value(w: dict | None) -> float | None:
    """Window value from our side: tributed gem lead, carried gems, deaths on both sides; a finished game
    counts its result."""
    if w is None:
        return None
    s = w.get('state')
    if s is None:
        if w['rounds'] < 1990 and w['win'] != 0.5:
            return TERMINAL * (2 * w['win'] - 1) + w['gems']
        return None
    v = s['tributed'][0] - s['tributed'][1]
    for alive, hp, max_hp, gems, gold in s['ours']:
        v += gems * 0.8 + 10.0 * hp / max(1, max_hp) if alive else -DEATH
    v += DEATH * sum(1 for e in s['enemies'] if not e)
    return v


def final(w: dict | None) -> float | None:
    if w is None:
        return None
    return w['win'] + 0.25 * math.tanh(w['gems'] / 400.0)


def mean_se(xs: list[float]) -> tuple[float, float]:
    n = len(xs)
    if n == 0:
        return 0.0, 0.0
    m = sum(xs) / n
    var = sum((x - m) ** 2 for x in xs) / max(1, n - 1)
    return m, math.sqrt(var / n)


def main() -> None:
    recs = []
    for name in sys.argv[1:]:
        recs += [json.loads(l) for l in (ROOT / '.cache/main/long/runs' / name / 'data.jsonl').open(encoding='utf-8')]
    print(f'{len(recs)} decisions')
    opts = sorted({int(k) for r in recs for k in r['win']})
    # chaos floor: noop vs as_is
    if 6 in opts:
        d = [value(r['win']['6']) - value(r['win']['0']) for r in recs if value(r['win']['0']) is not None and value(r['win']['6']) is not None]
        same = sum(1 for x in d if abs(x) < 1e-6)
        print(f'noop vs as_is: {same}/{len(d)} identical windows, |delta| mean {sum(abs(x) for x in d) / max(1, len(d)):.1f}')
        if 'full' in recs[0]:
            f = [(final(r['full']['6']), final(r['full']['0'])) for r in recs]
            f = [(a, b) for a, b in f if a is not None and b is not None]
            print(f'  final: {sum(1 for a, b in f if a == b)}/{len(f)} identical, result flips {sum(1 for a, b in f if round(a) != round(b))}')
    # advantages per option, overall and by v44's own stance
    print('option advantage over as_is (window value; mean +- se, share better/worse)')
    for k in opts:
        if k == 0:
            continue
        a = [value(r['win'][str(k)]) - value(r['win']['0']) for r in recs
             if value(r['win'][str(k)]) is not None and value(r['win']['0']) is not None]
        m, se = mean_se(a)
        print(f'  {NAMES[k]:12s} {m:+7.1f} +- {se:4.1f}  better {sum(x > 0.5 for x in a) / max(1, len(a)):.2f} worse {sum(x < -0.5 for x in a) / max(1, len(a)):.2f}  n {len(a)}')
    print('by v44 stance: n, mean advantage of each option')
    for s in sorted({r['v44'] for r in recs}):
        sub = [r for r in recs if r['v44'] == s]
        cells = []
        for k in opts:
            if k in (0, 6):
                continue
            a = [value(r['win'][str(k)]) - value(r['win']['0']) for r in sub
                 if value(r['win'][str(k)]) is not None and value(r['win']['0']) is not None]
            m, se = mean_se(a)
            cells.append(f'{NAMES[k]} {m:+.1f}({se:.1f})')
        print(f'  v44={NAMES[s]:11s} n {len(sub):4d}: ' + '  '.join(cells))
    # oracle: best option per decision
    gains = []
    for r in recs:
        v0 = value(r['win']['0'])
        if v0 is None:
            continue
        vs = [value(r['win'][str(k)]) for k in opts if k not in (0, 6)]
        vs = [v for v in vs if v is not None]
        if vs:
            gains.append(max(0.0, max(vs) - v0))
    m, se = mean_se(gains)
    print(f'oracle gain (best option per decision): {m:.1f} +- {se:.1f} value units')
    # window value vs final outcome
    if 'full' in recs[0]:
        agree = n = 0
        for r in recs:
            for k in opts:
                if k == 0:
                    continue
                dv = (value(r['win'][str(k)]) or 0) - (value(r['win']['0']) or 0)
                df = (final(r['full'][str(k)]) or 0) - (final(r['full']['0']) or 0)
                if abs(dv) > 20 and abs(df) > 0.05:
                    n += 1
                    agree += (dv > 0) == (df > 0)
        print(f'window vs final sign agreement (|dv|>20, |df|>0.05): {agree}/{n}')
        for k in opts:
            if k == 0:
                continue
            a = [final(r['full'][str(k)]) - final(r['full']['0']) for r in recs if r['full'].get(str(k)) and r['full'].get('0')]
            m, se = mean_se(a)
            print(f'  final {NAMES[k]:12s} {m:+.4f} +- {se:.4f}')


if __name__ == '__main__':
    main()
