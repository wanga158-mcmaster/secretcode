"""Parity gate: the bot's C++ inference (tools/netcheck.cpp) vs train.py's numpy forward on the same records.

  python -B -X utf8 bots/main/zero/tools/parity.py WEIGHTS.txt RECORDS.bin [--max 300]
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack  # noqa: E402
import train as T  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]
ap = argparse.ArgumentParser()
ap.add_argument('weights')
ap.add_argument('records')
ap.add_argument('--max', type=int, default=300)
a = ap.parse_args()
out = subprocess.run([str(ROOT / '.cache/main/zero/netcheck.exe'), a.weights, a.records, str(a.max)], capture_output=True, text=True, check=True).stdout
P = T.load(Path(a.weights))
recs = pack.read(Path(a.records))[:a.max]
lines = out.splitlines()
if len(lines) != len(recs) or not recs:
    raise SystemExit(f'expected {len(recs)} nonempty C++ outputs, received {len(lines)}')
worst = 0.0
for line, (head, n, label, team, rnd, won, state, phi) in zip(lines, recs):
    left, right = line.split(' | ')
    fields = left.split()
    if [int(x) for x in fields[:2]] != [head, n] or len(fields) != n + 2:
        raise SystemExit(f'C++ record shape does not match input: {left}')
    c_logits = np.array([float(x) for x in fields[2:]])
    c_value = float(right)
    state_batch = state[None, :].astype(np.float32)
    t, _ = T.draft_trunk(P, state_batch) if T.HEADS[head] == 'draft' else T.trunk(P, state_batch)
    logit, _ = T.head_forward(P, T.HEADS[head], t, phi[None, :, :].astype(np.float32), np.ones((1, n), dtype=bool))
    py_value = float(t[0] @ P['v'] + P['v0'][0])
    if not (np.all(np.isfinite(c_logits)) and np.isfinite(c_value) and np.all(np.isfinite(logit)) and np.isfinite(py_value)):
        raise SystemExit(f'non-finite inference output for head {head}')
    worst = max(worst, float(np.max(np.abs(logit[0] - c_logits))), abs(py_value - c_value))
print(f'{len(recs)} records; worst |C++ - numpy| = {worst:.2e}', 'PASS' if worst < 1e-4 else 'FAIL')
raise SystemExit(0 if worst < 1e-4 else 1)
