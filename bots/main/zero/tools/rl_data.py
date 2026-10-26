"""Decision-log parsing and the numpy policy maths for rl.py (train.py's forward/backward, imported unchanged).

A log line (bot.cpp log_decision): `Z head round roster n chosen logp <codec>` where codec (src/core/codec.h) is
2 chars per value: 48 state facts, then n x kPhi[head] candidate facts.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import train as T  # noqa: E402

KSTATE = T.KSTATE
LEVELS = 94 * 94
STEP = 8.0 / (LEVELS - 1)
NHEAD = len(T.LEGACY_HEADS)


def decode_values(s: bytes) -> np.ndarray:
    a = np.frombuffer(s, dtype=np.uint8).astype(np.int32) - 33
    q = a[0::2] * 94 + a[1::2]
    v = (-4.0 + q.astype(np.float64) * STEP)
    # The 94x94 grid has no exact 0 (nor integers): 0 decodes to +-0.00045, which saturated prior units (weight
    # 20, two layers) amplify into a 1.3 logit error. Facts within half a step of an integer are snapped to it
    # (one-hot and zero facts are the common case; the bot's dense layers skip exact zeros).
    r = np.round(v)
    v = np.where(np.abs(v - r) <= 0.5 * STEP + 1e-9, r, v)
    return v.astype(np.float32)


def parse_log(text: bytes) -> dict[int, list]:
    """Decision records per roster: [(head, round, n, chosen, logp, state[48], phi[n, F])] in log order."""
    out: dict[int, list] = {}
    for line in text.split(b'\n'):
        if not line.startswith(b'Z '):
            continue
        parts = line.rstrip(b'\r').split(b' ')
        if len(parts) != 8:
            continue
        head, rnd, roster, n, chosen = (int(x) for x in parts[1:6])
        logp = float(parts[6])
        if head >= NHEAD:
            continue
        F = T.KPHI[head]
        body = parts[7]
        if len(body) != 2 * (KSTATE + n * F):
            continue
        v = decode_values(body)
        out.setdefault(roster, []).append((head, rnd, n, chosen, logp, v[:KSTATE], v[KSTATE:].reshape(n, F)))
    return out


class Batch:
    """Decision records of one head, padded to the head's widest candidate set."""

    def __init__(self, head: int, recs: list):
        self.head = head
        self.name = T.LEGACY_HEADS[head]
        F = T.KPHI[head]
        C = max(r['n'] for r in recs)
        B = len(recs)
        self.state = np.stack([r['state'] for r in recs]).astype(np.float32)
        self.phi = np.zeros((B, C, F), dtype=np.float32)
        for i, r in enumerate(recs):
            self.phi[i, :r['n']] = r['phi']
        self.n = np.array([r['n'] for r in recs])
        self.mask = np.arange(C)[None, :] < self.n[:, None]
        self.chosen = np.array([r['chosen'] for r in recs])
        self.old_logp = np.array([r['logp'] for r in recs], dtype=np.float32)
        self.adv = np.array([r.get('adv', 0.0) for r in recs], dtype=np.float32)
        self.ret = np.array([r.get('ret', 0.0) for r in recs], dtype=np.float32)

    def take(self, j):
        b = object.__new__(Batch)
        b.head, b.name = self.head, self.name
        for k in ('state', 'phi', 'n', 'mask', 'chosen', 'old_logp', 'adv', 'ret'):
            setattr(b, k, getattr(self, k)[j])
        return b


def log_softmax(logit, mask):
    mx = np.where(mask, logit, -np.inf).max(1, keepdims=True)
    e = np.exp(np.where(mask, logit - mx, -np.inf))
    z = e.sum(1, keepdims=True)
    return np.where(mask, logit - mx - np.log(z), -np.inf), e / z


TAU = 1.0  # sampling temperature (config[0]); the bot's logp is log softmax(logit / TAU)


def forward(P, b: Batch):
    """Returns the temperature-scaled logits (logit / TAU)."""
    t, cache = T.trunk(P, b.state)
    logit, hc = T.head_forward(P, b.name, t, b.phi, b.mask)
    return t, cache, logit / TAU, hc


def policy_logp(P, b: Batch):
    _, _, logit, _ = forward(P, b)
    lp, _ = log_softmax(logit, b.mask)
    return lp[np.arange(len(b.chosen)), b.chosen], lp


def head_backward(P, b: Batch, t, hc, d, G):
    """d = dLoss/dlogit [B, C]; accumulates head gradients into G and returns dLoss/dt (train.head_loss_back's
    chain, generalised from cross-entropy to any logit gradient)."""
    name = b.name
    u, v, z, g = hc
    du = np.einsum('bc,bck->bk', d, g)
    dz = (d[:, :, None] * u[:, None, :]) * T.dss(z)
    G[f'p_{name}'] += np.einsum('bcf,bck->fk', b.phi, dz)
    G[f'c_{name}'] += dz.sum((0, 1))
    dv = np.einsum('bc,bcf->bf', d, b.phi)
    G[f'u_{name}'] += t.T @ du
    G[f'vv_{name}'] += t.T @ dv
    return du @ P[f'u_{name}'].T + dv @ P[f'vv_{name}'].T


def ppo_step_grads(P, b: Batch, G, clip=0.2, vcoef=0.5, ent=0.0, value_trunk=False):
    """PPO-clip surrogate (minimised: -min(r A, clip(r) A)), value MSE of the trunk value head v.t + v0 on the
    return (the bot never reads v), optional entropy bonus. Returns stats; accumulates G (mean over the batch)."""
    B = len(b.chosen)
    t, cache, logit, hc = forward(P, b)
    lp_all, p = log_softmax(logit, b.mask)
    idx = np.arange(B)
    lp = lp_all[idx, b.chosen]
    ratio = np.exp(lp - b.old_logp)
    A = b.adv
    unclipped = ratio * A
    clipped = np.clip(ratio, 1 - clip, 1 + clip) * A
    active = unclipped <= clipped  # gradient flows through the unclipped term where it is the min
    # d(-ratio*A)/dlp = -ratio*A where active; dlp/dlogit = onehot - p
    g_lp = np.where(active, -ratio * A, 0.0) / B
    onehot = np.zeros_like(p)
    onehot[idx, b.chosen] = 1.0
    d = g_lp[:, None] * (onehot - p)
    pz = np.where(b.mask, p, 0.0)
    lpz = np.where(b.mask, lp_all, 0.0)
    entropy = -(pz * lpz).sum(1)
    if ent:
        # minimise -ent*H: dH/dlogit_c = -p_c (log p_c + H)
        d += (ent / B) * pz * (lpz + entropy[:, None])
    d = np.where(b.mask, d, 0.0) / TAU  # back through logit / TAU
    dt = head_backward(P, b, t, hc, d, G)
    # Value head (P['v'], P['v0']) regresses the return.
    val = t @ P['v'] + P['v0'][0]
    err = val - b.ret
    dval = vcoef * 2.0 * err / B
    G['v'] += t.T @ dval
    G['v0'] += dval.sum()
    if value_trunk:  # by default the baseline does not move the policy's trunk
        dt = dt + dval[:, None] * P['v'][None, :]
    T.trunk_back(P, cache, dt, G)
    return {'n': B, 'ratio_dev': float(np.abs(ratio - 1).mean()), 'clipfrac': float((~active).mean()),
            'entropy': float(entropy.mean()), 'vloss': float((err ** 2).mean()),
            'kl': float((b.old_logp - lp).mean())}


def value_of(P, state: np.ndarray) -> np.ndarray:
    t, _ = T.trunk(P, state.astype(np.float32))
    return t @ P['v'] + P['v0'][0]
