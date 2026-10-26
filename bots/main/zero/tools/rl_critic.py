"""Training-only critic for rl.py's GAE (--gae): a numpy MLP V(state) over the bot's 48 state facts, never
deployed (the bot does not use values), so its size is not fuel-bounded. tanh hidden layers, Adam, MSE.
Persisted next to the run state as critic.npz."""
from __future__ import annotations

from pathlib import Path

import numpy as np


class Critic:
    def __init__(self, n_in: int = 48, hidden=(128, 128), seed: int = 1):
        rng = np.random.default_rng(seed)
        sizes = [n_in, *hidden, 1]
        self.W = [(rng.standard_normal((a, b)) * np.sqrt(1.0 / a)).astype(np.float32) for a, b in zip(sizes, sizes[1:])]
        self.b = [np.zeros(b, dtype=np.float32) for b in sizes[1:]]
        self.W[-1] *= 0.0
        self.m = [np.zeros_like(x) for x in self.W + self.b]
        self.v = [np.zeros_like(x) for x in self.W + self.b]
        self.t = 0
        self.fits = 0

    def predict(self, X: np.ndarray) -> np.ndarray:
        h = X.astype(np.float32)
        for W, b in zip(self.W[:-1], self.b[:-1]):
            h = np.tanh(h @ W + b)
        return (h @ self.W[-1] + self.b[-1])[:, 0]

    def fit(self, X: np.ndarray, y: np.ndarray, epochs: int = 4, batch: int = 1024, lr: float = 1e-3, seed: int = 0) -> float:
        rng = np.random.default_rng(seed)
        X = X.astype(np.float32)
        y = y.astype(np.float32)
        n = len(X)
        loss = 0.0
        for _ in range(epochs):
            perm = rng.permutation(n)
            for s in range(0, n, batch):
                j = perm[s:s + batch]
                hs = [X[j]]
                for W, b in zip(self.W[:-1], self.b[:-1]):
                    hs.append(np.tanh(hs[-1] @ W + b))
                out = (hs[-1] @ self.W[-1] + self.b[-1])[:, 0]
                err = out - y[j]
                loss = float((err ** 2).mean())
                g = (2.0 * err / len(j))[:, None]
                gW, gb = [None] * len(self.W), [None] * len(self.b)
                for L in range(len(self.W) - 1, -1, -1):
                    gW[L] = hs[L].T @ g
                    gb[L] = g.sum(0)
                    if L:
                        g = (g @ self.W[L].T) * (1.0 - hs[L] ** 2)
                self.t += 1
                for k, (p, gp) in enumerate(zip(self.W + self.b, gW + gb)):
                    self.m[k] = 0.9 * self.m[k] + 0.1 * gp
                    self.v[k] = 0.999 * self.v[k] + 0.001 * gp * gp
                    mh = self.m[k] / (1 - 0.9 ** self.t)
                    vh = self.v[k] / (1 - 0.999 ** self.t)
                    p -= (lr * mh / (np.sqrt(vh) + 1e-8)).astype(np.float32)
        self.fits += 1
        return loss

    def save(self, path: Path) -> None:
        arrs = {f'W{i}': w for i, w in enumerate(self.W)} | {f'b{i}': b for i, b in enumerate(self.b)}
        arrs |= {f'm{i}': x for i, x in enumerate(self.m)} | {f'v{i}': x for i, x in enumerate(self.v)}
        np.savez(path, t=self.t, fits=self.fits, n=len(self.W), **arrs)

    @classmethod
    def load(cls, path: Path) -> 'Critic':
        z = np.load(path)
        c = object.__new__(cls)
        n = int(z['n'])
        c.W = [z[f'W{i}'] for i in range(n)]
        c.b = [z[f'b{i}'] for i in range(n)]
        c.m = [z[f'm{i}'] for i in range(2 * n)]
        c.v = [z[f'v{i}'] for i in range(2 * n)]
        c.t, c.fits = int(z['t']), int(z['fits'])
        return c
