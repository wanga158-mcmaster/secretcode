"""Finite-difference check of train.py's hand-written gradients (trunk + one head + value), float64.

  python -B -X utf8 bots/main/zero/tools/gradcheck.py [--head ranged]
"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train as T  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--head', choices=T.HEADS, default='goal')
a = ap.parse_args()
rng = np.random.default_rng(0)
P = {k: v.astype(np.float64) for k, v in T.init(rng).items()}
for k in P:
    P[k] += rng.standard_normal(P[k].shape) * 0.1
name = a.head
forward_trunk = T.draft_trunk if name == 'draft' else T.trunk
backward_trunk = T.draft_trunk_back if name == 'draft' else T.trunk_back
F, B, C = T.KPHI[T.HEADS.index(name)], 5, 24 if name in ('ranged', 'action', 'aux') else 6
if name in T.SCALAR_HEADS:
    C = 1
s = rng.standard_normal((B, T.KSTATE))
phi = rng.standard_normal((B, C, F))
n = np.array([C, C // 2, C - 1, 2, C])
mask = np.arange(C)[None, :] < n[:, None]
label = np.array([1, 0, C - 2, 1, C - 1])
if name == 'priority':
    phi[:] = [1, 0, 0, 0]
    n[:] = 1
    mask[:] = True
    label = np.array([T.PRIORITY_MIN, -12345, 0, 150, T.PRIORITY_MAX], dtype=np.int64)
elif name == 'quantity':
    phi[:] = np.array([[[1, 0.1, 0.1, 1]], [[1, 0.4, 0.1, 1]], [[1, 0.8, 0.1, 1]], [[1, 0.1, 0.1, 1]], [[1, 0.4, 0.1, 1]]])
    n[:] = 1
    mask[:] = True
    label = np.array([1, 2, 5, 10, 100], dtype=np.int64)
won = np.array([1, 0, 1, -1, 0])
vw = 0.5


def total_loss(P):
    t, _ = forward_trunk(P, s)
    logit, _ = T.head_forward(P, name, t, phi, mask)
    if name in T.SCALAR_HEADS:
        ce = np.square(logit[:, 0] - T.scalar_target(name, label)).mean()
    else:
        mx = logit.max(1, keepdims=True)
        e = np.exp(logit - mx) * mask
        p = e / e.sum(1, keepdims=True)
        ce = -np.log(p[np.arange(B), label]).mean()
    ok = won >= 0
    vl = t @ P['v'] + P['v0'][0]
    y = (won == 1).astype(float)
    bce = -(y * np.log(1 / (1 + np.exp(-vl))) + (1 - y) * np.log(1 - 1 / (1 + np.exp(-vl))))
    return ce + vw * (bce * ok).sum() / ok.sum()


G = {k: np.zeros_like(v) for k, v in P.items()}
t, cache = forward_trunk(P, s)
fwd = T.head_forward(P, name, t, phi, mask)
_, _, dt = T.head_loss_back(P, name, t, phi, mask, label, fwd, G, 1.0)
ok = won >= 0
vl = t @ P['v'] + P['v0'][0]
pv = 1 / (1 + np.exp(-vl))
dv = np.where(ok, pv - (won == 1), 0.0) * vw / ok.sum()
G['v'] += t.T @ dv
G['v0'] += dv.sum()
dt = dt + dv[:, None] * P['v'][None, :]
backward_trunk(P, cache, dt, G)
worst = 0.0
trunk_params = ['dw1', 'db1'] if name == 'draft' else ['w1', 'b1', 'w2', 'b2']
for k in trunk_params + ['v', 'v0', f'u_{name}', f'vv_{name}', f'p_{name}', f'c_{name}']:
    flat = P[k].reshape(-1)
    indices = rng.choice(flat.size, size=min(6, flat.size), replace=False)
    if name in T.SCALAR_HEADS:
        indices = np.unique(np.append(indices, 0))  # include the active constant-feature column
    for idx in indices:
        old = flat[idx]
        flat[idx] = old + 1e-6
        lp = total_loss(P)
        flat[idx] = old - 1e-6
        lm = total_loss(P)
        flat[idx] = old
        num = (lp - lm) / 2e-6
        ana = G[k].reshape(-1)[idx]
        rel = abs(num - ana) / max(1e-8, abs(num) + abs(ana))
        worst = max(worst, rel)
    print(f'{k:10s} ok (sampled)')
print(f'worst relative error {worst:.2e}', 'PASS' if worst < 1e-5 else 'FAIL')
if name == 'draft':
    assert all(not np.any(G[k]) for k in ('w1', 'b1', 'w2', 'b2'))
    print('draft gradients leave the gameplay trunk unchanged PASS')
if name == 'priority':
    boundary = np.array([T.PRIORITY_MIN, -1000000, -1, 0, 1, 1000000, T.PRIORITY_MAX], dtype=np.int64)
    assert np.array_equal(T.priority_raw(T.priority_target(boundary)), boundary)
    assert np.array_equal(T.priority_raw([-1e6, 1e6]), [T.PRIORITY_MIN, T.PRIORITY_MAX])
    print('priority normalization/inverse int32 boundaries PASS')
elif name == 'quantity':
    boundary = np.array([1, 2, 5, 100, 1000000, T.PRIORITY_MAX], dtype=np.int64)
    assert np.array_equal(T.quantity_raw(T.quantity_target(boundary)), boundary)
    assert np.array_equal(T.quantity_raw([-1e6, 1e6]), [1, T.PRIORITY_MAX])
    for invalid in (0, -1, T.PRIORITY_MAX + 1, 0.5):
        try:
            T.quantity_target(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid quantity accepted')
    try:
        T.head_loss_back(P, name, t, phi, np.zeros_like(mask), label, fwd, G, 1.0)
    except ValueError:
        pass
    else:
        raise AssertionError('masked scalar quantity accepted')
    print('quantity positive normalization/inverse boundaries and mask rejection PASS')
raise SystemExit(0 if worst < 1e-5 else 1)
