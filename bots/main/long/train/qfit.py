"""S1-Q value fit (M1): option values for farm-target decisions from qdata.py records.

Target per decision (the episode that followed the choice):
  y = dgems + kappa x dgold - lambda x rounds - death x (lambda x DOWNTIME + carried gems)
with lambda the mean gems-eq per episode-round over the data (the average-reward currency). Models: ridge on the
features below, and a histogram gradient-boosted tree ensemble (numpy). Validation by map (no map in both sets);
the yardstick is v44's own score s, which the bot maximises today.

  python -B -X utf8 bots/main/long/train/qfit.py --data q1 [--trees 200] [--depth 3] [--out W.json]
"""
from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / '.cache/main/long/data'
DOWNTIME = 40  # rounds a death costs (respawn wait + walk back), in lambda units
NAMES = ['s', 'travel/40', 'gems/50', 'gold/200', 'camp_hp/5000', 'camp_str/200', 'need', 'committed', 'enemy',
         'st_alive', 'st_reward', 'st_resp', 'rank', 'hp_frac', 'max_hp/2000', 'gold_me/300', 'gems_me/50', 'round/1000',
         'value/travel', 'harmless', 'hp_ratio']


def features(q: dict, c: dict, rank: int) -> list[float]:
    st = int(c['status'])
    value = c['gems'] + c['gold'] / 10.0
    return [c['s'], c['travel'] / 40.0, c['gems'] / 50.0, c['gold'] / 200.0, c['hp'] / 5000.0, c['str'] / 200.0, c['need'],
            c['committed'], c['enemy'], float(st == 0), float(st == 1), float(st == 2), float(rank), q['hp'] / max(1.0, q['max_hp']),
            q['max_hp'] / 2000.0, q['gold'] / 300.0, q['gems'] / 50.0, q['round'] / 1000.0, value / max(5.0, c['travel'] + 5.0),
            float(c['str'] <= 0), c['hp'] / max(1.0, q['hp'])]


def load(name: str, kappa: float = 0.1):
    recs = [json.loads(l) for l in (DATA / f'{name}.jsonl').read_text(encoding='utf-8').splitlines()]
    recs = [r for r in recs if r.get('out') is not None]
    lam = sum(r['out']['dgems'] + kappa * r['out']['dgold'] for r in recs) / max(1.0, sum(max(1.0, r['out']['len']) for r in recs))
    X, y, maps, s = [], [], [], []
    for r in recs:
        k = int(r['chosen'])
        c = r['cands'][k]
        o = r['out']
        X.append(features(r, c, k))
        y.append(o['dgems'] + kappa * o['dgold'] - lam * max(1.0, o['len']) - o['died'] * (lam * DOWNTIME + r['gems']))
        maps.append(r['map'])
        s.append(c['s'])
    return np.array(X), np.array(y), maps, np.array(s), lam, recs


def ridge(X, y, l2=1.0):
    Z = np.column_stack([np.ones(len(X)), X])
    return np.linalg.solve(Z.T @ Z + l2 * np.eye(Z.shape[1]), Z.T @ y)


class GBM:
    """Least-squares gradient boosting with depth-limited trees on 32 quantile bins per feature."""

    def __init__(self, trees=200, depth=3, lr=0.1, min_leaf=200, bins=32):
        self.trees, self.depth, self.lr, self.min_leaf, self.bins = trees, depth, lr, min_leaf, bins

    def fit(self, X, y):
        self.edges = [np.unique(np.quantile(X[:, j], np.linspace(0, 1, self.bins + 1)[1:-1])) for j in range(X.shape[1])]
        B = np.column_stack([np.searchsorted(self.edges[j], X[:, j], side='right') for j in range(X.shape[1])])
        self.base = float(y.mean())
        pred = np.full(len(y), self.base)
        self.forest = []
        for _ in range(self.trees):
            res = y - pred
            tree = self._grow(B, res, np.arange(len(y)), 0)
            self.forest.append(tree)
            pred += self.lr * self._apply(tree, B)
        return self

    def _grow(self, B, r, idx, d):
        if d == self.depth or len(idx) < 2 * self.min_leaf:
            return float(r[idx].mean())
        best = None
        tot, n = r[idx].sum(), len(idx)
        for j in range(B.shape[1]):
            nb = len(self.edges[j]) + 1
            cnt = np.bincount(B[idx, j], minlength=nb)
            sm = np.bincount(B[idx, j], weights=r[idx], minlength=nb)
            cc, cs = np.cumsum(cnt)[:-1], np.cumsum(sm)[:-1]
            ok = (cc >= self.min_leaf) & (n - cc >= self.min_leaf)
            if not ok.any():
                continue
            gain = np.where(ok, cs ** 2 / np.maximum(cc, 1) + (tot - cs) ** 2 / np.maximum(n - cc, 1), -np.inf)
            b = int(np.argmax(gain))
            if best is None or gain[b] > best[0]:
                best = (gain[b], j, b)
        if best is None:
            return float(r[idx].mean())
        _, j, b = best
        left = idx[B[idx, j] <= b]
        right = idx[B[idx, j] > b]
        return (j, b, self._grow(B, r, left, d + 1), self._grow(B, r, right, d + 1))

    def _apply(self, tree, B):
        out = np.empty(len(B))
        stack = [(tree, np.arange(len(B)))]
        while stack:
            t, idx = stack.pop()
            if not isinstance(t, tuple):
                out[idx] = t
                continue
            j, b, l, r = t
            m = B[idx, j] <= b
            stack.append((l, idx[m]))
            stack.append((r, idx[~m]))
        return out

    def predict(self, X):
        B = np.column_stack([np.searchsorted(self.edges[j], X[:, j], side='right') for j in range(X.shape[1])])
        return self.base + self.lr * sum(self._apply(t, B) for t in self.forest)


def corr(a, b):
    return float(np.corrcoef(a, b)[0, 1])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', required=True)
    ap.add_argument('--trees', type=int, default=200)
    ap.add_argument('--depth', type=int, default=3)
    ap.add_argument('--out')
    ap.add_argument('--kappa', type=float, default=0.1, help='gems-equivalent per gold in the currency')
    ap.add_argument('--epsilon', type=float, default=0.2, help='the logging run qlog_epsilon')
    ap.add_argument('--header', help='write the GBM as a C++ header (bots/main/long/src/q_model.h)')
    a = ap.parse_args()
    X, y, maps, s, lam, recs = load(a.data, a.kappa)
    names = sorted(set(maps))
    random.Random(3).shuffle(names)
    test_maps = set(names[::4])
    te = np.array([m in test_maps for m in maps])
    print(f'{len(y)} decisions on {len(names)} maps (test {len(test_maps)} maps, {te.sum()} decisions); lambda {lam:.4f} gems-eq/round')
    print(f'  y mean {y.mean():+.2f} sd {y.std():.2f}; explored (chosen > 0) {np.mean(X[:, NAMES.index("rank")] > 0):.2f}')
    print(f'  held-out corr with y: v44 score s {corr(s[te], y[te]):+.3f}')
    w = ridge(X[~te], y[~te])
    pr = np.column_stack([np.ones(te.sum()), X[te]]) @ w
    print(f'  ridge {corr(pr, y[te]):+.3f}')
    g = GBM(trees=a.trees, depth=a.depth).fit(X[~te], y[~te])
    pg = g.predict(X[te])
    print(f'  gbm ({a.trees} trees, depth {a.depth}) {corr(pg, y[te]):+.3f}; R2 {1 - np.mean((pg - y[te]) ** 2) / np.var(y[te]):.3f}')
    # Decision quality (off-policy, inverse propensity): the logging policy chose rank 0 with prob (1 - eps) + eps / k
    # and rank i > 0 with eps / k. The value of a deterministic policy pi on held-out decisions is
    # mean(y x [pi(s) = chosen] / P(chosen)); v44 is pi = rank 0.
    eps = a.epsilon
    test_recs = [r for r, t in zip(recs, te) if t]
    yt = y[te]
    v44, ridge_v, gbm_v, oracle = [], [], [], []
    for r, yy in zip(test_recs, yt):
        k = len(r['cands'])
        ch = int(r['chosen'])
        p = (1 - eps) + eps / k if ch == 0 else eps / k
        F = np.array([features(r, c, i) for i, c in enumerate(r['cands'])])
        qr = np.column_stack([np.ones(k), F]) @ w
        qg = g.predict(F)
        v44.append(yy * (ch == 0) / p)
        ridge_v.append(yy * (int(np.argmax(qr)) == ch) / p)
        gbm_v.append(yy * (int(np.argmax(qg)) == ch) / p)
    se = lambda v: float(np.std(v) / math.sqrt(len(v)))
    print(f'  off-policy value per decision (gems-eq net of lambda x rounds; eps {eps}): v44 {np.mean(v44):+.2f} +- {se(v44):.2f}, '
          f'ridge-argmax {np.mean(ridge_v):+.2f} +- {se(ridge_v):.2f}, gbm-argmax {np.mean(gbm_v):+.2f} +- {se(gbm_v):.2f}')
    d = np.array(gbm_v) - np.array(v44)
    print(f'  gbm - v44 {d.mean():+.2f} +- {se(d):.2f} (paired)')
    for n, v in sorted(zip(['bias'] + NAMES, w), key=lambda t: -abs(t[1]))[:10]:
        print(f'    ridge {n:14s} {v:+.3f}')
    if a.out:
        Path(a.out).write_text(json.dumps({'lambda': lam, 'names': NAMES, 'ridge': list(map(float, w))}), encoding='utf-8')
    if a.header:
        write_header(g, Path(a.header), f'held-out corr {corr(pg, y[te]):.3f}, off-policy gain over v44 {d.mean():+.2f} +- {se(d):.2f}')


def write_header(g: GBM, path: Path, note: str) -> None:
    """Flatten the forest into complete depth-D trees: node arrays (feature, threshold) and leaf values; a split
    "bin <= b" is "x < edges[b]" (searchsorted right), +inf past the last edge."""
    D = g.depth
    feats, thrs, leaves = [], [], []

    def fill(t, node, depth, nf, nt, lv):
        if depth == D:
            lv[node - (2 ** D - 1)] = g.lr * (t if not isinstance(t, tuple) else 0.0)
            return
        if not isinstance(t, tuple):  # an early leaf: a dummy split, both children the same value
            nf[node], nt[node] = 0, 1e30
            fill(t, 2 * node + 1, depth + 1, nf, nt, lv)
            fill(t, 2 * node + 2, depth + 1, nf, nt, lv)
            return
        j, b, l, r = t
        e = g.edges[j]
        nf[node], nt[node] = j, float(e[b]) if b < len(e) else 1e30
        fill(l, 2 * node + 1, depth + 1, nf, nt, lv)
        fill(r, 2 * node + 2, depth + 1, nf, nt, lv)
    for t in g.forest:
        nf, nt, lv = [0] * (2 ** D - 1), [0.0] * (2 ** D - 1), [0.0] * (2 ** D)
        fill(t, 0, 0, nf, nt, lv)
        feats += nf
        thrs += nt
        leaves += lv
    def fmt(v: float) -> str:  # a valid float literal: "1f" is not C++, "1.0f" / "1e-05f" are
        if v >= 1e29:
            return '1e30f'
        s = f'{v:.6g}'
        return (s if ('.' in s or 'e' in s) else s + '.0') + 'f'
    lines = ['#pragma once', '// Generated by bots/main/long/train/qfit.py: farm-target option value (S1-Q), boosted trees.',
             f'// {note}. Features: ' + ', '.join(NAMES) + '.',
             'namespace mb::qmodel {', f'inline constexpr int kTrees = {len(g.forest)}, kDepth = {D}, kFeatures = {len(NAMES)};',
             f'inline constexpr float kBase = {g.base:.6g}f;',
             'inline constexpr unsigned char kFeat[] = { ' + ', '.join(map(str, feats)) + ' };',
             'inline constexpr float kThr[] = { ' + ', '.join(map(fmt, thrs)) + ' };',
             'inline constexpr float kLeaf[] = { ' + ', '.join(map(fmt, leaves)) + ' };',
             'inline float value(const float* x)', '{', '    float v = kBase;',
             '    for (int t = 0; t < kTrees; ++t)', '    {', '        int n = 0;',
             '        for (int d = 0; d < kDepth; ++d) n = 2 * n + (x[kFeat[t * ((1 << kDepth) - 1) + n]] < kThr[t * ((1 << kDepth) - 1) + n] ? 1 : 2);',
             '        v += kLeaf[t * (1 << kDepth) + n - ((1 << kDepth) - 1)];', '    }', '    return v;', '}',
             '} // namespace mb::qmodel', '']
    path.write_bytes('\n'.join(lines).encode('utf-8'))
    print(f'wrote {path} ({len(g.forest)} trees)')


if __name__ == '__main__':
    main()
