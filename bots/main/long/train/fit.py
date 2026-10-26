"""Fit the contact-stance scorer (learn.h stance block) to forks.py advantages and write a weight file.

  python -B -X utf8 bots/main/long/train/fit.py RUN[,RUN...] --out W.txt [--ridge L] [--margin M] [--folds K]
         [--init W.txt]

Per option k (1-5) a ridge regression of A_k = V_k - V_as_is (analyse.value, the window value) on the 32
logged features plus a bias. Cross-validation by map (K folds): on held-out maps the policy plays argmax_k
of the predicted advantage when it exceeds the margin; the realized advantage of that choice is the score.
The weights file holds the 300-value learned block: --init's weights (other blocks) plus the stance block.
"""
from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyse import value  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
COUNT, STANCE, SLOTS = 396, 102, 48  # learn.h kCount, kOffset[stance], fork.h kFeatures
NF = SLOTS  # features used (set from the data: older runs logged 32)
CLIP = 100.0  # advantages are clipped to +-CLIP value units (heavy tails: decided games, multi-kill swings)
OPTIONS = [1, 2, 3, 4, 5]


def load(runs: str) -> list[dict]:
    recs = []
    for name in runs.split(','):
        recs += [json.loads(l) for l in (ROOT / '.cache/main/long/runs' / name / 'data.jsonl').open(encoding='utf-8')]
    out = []
    for r in recs:
        v0 = value(r['win'].get('0'))
        if v0 is None:
            continue
        adv, raw = {}, {}
        for k in OPTIONS:
            v = value(r['win'].get(str(k)))
            if v is not None:
                adv[k] = max(-CLIP, min(CLIP, v - v0))
                raw[k] = v - v0
        r['adv'] = adv  # fitted (clipped)
        r['raw'] = raw  # scored
        out.append(r)
    global NF
    NF = min(len(r['x']) for r in out)
    return out


def design(recs: list[dict]) -> np.ndarray:
    return np.array([[1.0] + r['x'][:NF] for r in recs])


def fit(recs: list[dict], ridge: float) -> dict[int, np.ndarray]:
    w = {}
    for k in OPTIONS:
        sub = [r for r in recs if k in r['adv']]
        X = design(sub)
        y = np.array([r['adv'][k] for r in sub])
        reg = ridge * np.eye(X.shape[1])
        reg[0, 0] = 0.0
        w[k] = np.linalg.solve(X.T @ X + reg, X.T @ y)
    return w


def policy_gain(recs: list[dict], w: dict[int, np.ndarray], margin: float) -> tuple[float, float, float]:
    """Mean realized advantage of the policy's choice, the share of decisions it changes, the oracle."""
    X = design(recs)
    gains, changed, oracle = [], 0, []
    for i, r in enumerate(recs):
        best, best_s = 0, margin
        for k in OPTIONS:
            s = float(X[i] @ w[k])
            if s > best_s and k in r['adv']:
                best, best_s = k, s
        gains.append(r['raw'][best] if best else 0.0)
        changed += best != 0
        oracle.append(max([0.0] + list(r['raw'].values())))
    n = max(1, len(recs))
    return sum(gains) / n, changed / n, sum(oracle) / n


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('runs')
    p.add_argument('--out')
    p.add_argument('--ridge', type=float, default=10.0)
    p.add_argument('--margin', type=float, default=5.0)
    p.add_argument('--folds', type=int, default=5)
    p.add_argument('--init')
    p.add_argument('--seed', type=int, default=1)
    a = p.parse_args()
    recs = load(a.runs)
    maps = sorted({r['map'] for r in recs})
    random.Random(a.seed).shuffle(maps)
    print(f'{len(recs)} decisions on {len(maps)} maps')
    for ridge in sorted({a.ridge, 1.0, 100.0, 1000.0}):
        for margin in sorted({a.margin, 0.0, 10.0, 20.0}):
            tot = ch = orc = 0.0
            n = 0
            for f in range(a.folds):
                test_maps = set(maps[f::a.folds])
                train = [r for r in recs if r['map'] not in test_maps]
                test = [r for r in recs if r['map'] in test_maps]
                g, c, o = policy_gain(test, fit(train, ridge), margin)
                tot += g * len(test)
                ch += c * len(test)
                orc += o * len(test)
                n += len(test)
            print(f'ridge {ridge:7.1f} margin {margin:5.1f}: held-out gain {tot / n:+6.2f} per decision, changes {ch / n:.2f}, oracle {orc / n:.1f}')
    w = fit(recs, a.ridge)
    g, c, o = policy_gain(recs, w, a.margin)
    print(f'in-sample (ridge {a.ridge}, margin {a.margin}): gain {g:+.2f}, changes {c:.2f}')
    if a.out:
        base = [float(x) for x in Path(a.init).read_text(encoding='utf-8').split()] if a.init else []
        out = (base + [0.0] * COUNT)[:COUNT]
        out[94:102] = [0.0] * 8  # never ship a directive
        for k in OPTIONS:
            at = STANCE + (k - 1) * (SLOTS + 1)
            out[at:at + NF + 1] = [float(x) for x in w[k]]
        Path(a.out).write_text(' '.join(f'{x:.6g}' for x in out) + '\n', encoding='utf-8')
        print(f'wrote {a.out}')


if __name__ == '__main__':
    main()
