"""How well a logistic model on the logged contact features predicts the final result, by round band (AUC on
held-out maps). Input: forks.py datasets (features x, base_final win).

  python -B -X utf8 bots/main/long/train/auc.py RUN[,RUN...]
"""
from __future__ import annotations

import json
import random
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]


def auc(score: np.ndarray, y: np.ndarray) -> float:
    pos, neg = score[y == 1], score[y == 0]
    if len(pos) == 0 or len(neg) == 0:
        return float('nan')
    order = np.argsort(np.concatenate([pos, neg]))
    ranks = np.empty(len(order))
    ranks[order] = np.arange(1, len(order) + 1)
    return float((ranks[:len(pos)].sum() - len(pos) * (len(pos) + 1) / 2) / (len(pos) * len(neg)))


def logistic(X: np.ndarray, y: np.ndarray, l2: float = 1.0, iters: int = 300) -> np.ndarray:
    w = np.zeros(X.shape[1])
    for _ in range(iters):
        p = 1 / (1 + np.exp(-X @ w))
        g = X.T @ (p - y) / len(y) + l2 * w / len(y)
        h = (X * (p * (1 - p))[:, None]).T @ X / len(y) + l2 * np.eye(X.shape[1]) / len(y)
        w -= np.linalg.solve(h, g)
    return w


def main() -> None:
    recs = []
    for name in sys.argv[1].split(','):
        recs += [json.loads(l) for l in (ROOT / '.cache/main/long/runs' / name / 'data.jsonl').open(encoding='utf-8')]
    recs = [r for r in recs if r['base_final']['win'] in (0, 1)]
    nf = min(len(r['x']) for r in recs)
    maps = sorted({r['map'] for r in recs})
    random.Random(1).shuffle(maps)
    test = set(maps[::4])
    for lo, hi in ((0, 200), (200, 400), (400, 700), (700, 2000)):
        sub = [r for r in recs if lo <= r['round'] < hi]
        tr = [r for r in sub if r['map'] not in test]
        te = [r for r in sub if r['map'] in test]
        if len(tr) < 50 or len(te) < 20:
            continue
        X = lambda rs: np.array([[1.0] + r['x'][:nf] for r in rs])
        y = lambda rs: np.array([float(r['base_final']['win']) for r in rs])
        w = logistic(X(tr), y(tr))
        print(f'rounds {lo:4d}-{hi:4d}: train {len(tr):5d} test {len(te):4d}  held-out AUC {auc(X(te) @ w, y(te)):.3f}  '
              f'(gem gap alone {auc(np.array([r["x"][10] for r in te]), y(te)):.3f})')


if __name__ == '__main__':
    main()
