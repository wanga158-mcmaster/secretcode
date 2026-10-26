"""Self-test of opt.py's algorithms without games: maximise a noisy concave quadratic (optimum at x* = 1..)."""
import random
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import opt  # noqa: E402

N = 12
STAR = [1.0] * N


def f(x, rng):
    return -sum((xi - si) ** 2 for xi, si in zip(x, STAR)) / N + rng.gauss(0.0, 0.05)


for algo, (ask, tell) in opt.ALGOS.items():
    a = SimpleNamespace(pop=8, step=0.3, lr=1.0, gens=60)
    st = {'gen': 0, 'x': [0.0] * N}
    noise = random.Random(7)
    for g in range(a.gens):
        rng = random.Random(1000 + g)
        cands = ask(st, rng, a)
        st['_cands'] = cands
        tell(st, {t: f(x, noise) for t, x in cands.items()}, a)
        st['gen'] += 1
    err = sum((xi - 1.0) ** 2 for xi in st['x']) / N
    print(f'{algo:5s} mean squared error to optimum after {a.gens} gens: {err:.3f} (start 1.000)')
