"""Turns at or over the fuel budget in a .fuel stream (arena bundle): index, approximate round, fuel.

  python -B -X utf8 bots/main/long/train/fuelpeek.py PATH.fuel [--bodies 5] [--top 10]
"""
import argparse
import struct
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('path')
ap.add_argument('--bodies', type=int, default=5)
ap.add_argument('--top', type=int, default=10)
a = ap.parse_args()
raw = Path(a.path).read_bytes()
values = struct.unpack_from(f"<{(len(raw) - 13) // 8}Q", raw, 13)
budgets, turns = values[:8], values[8:]
budget = budgets[3] if budgets[3] else 100000
print('budgets', budgets, 'turns', len(turns))
over = [(i, v) for i, v in enumerate(turns) if v >= budget]
print('over budget', len(over), [(i, i // a.bodies, v) for i, v in over[:20]])
for i, v in sorted(enumerate(turns), key=lambda t: -t[1])[:a.top]:
    print(f'  turn {i} ~r{i // a.bodies} fuel {v}')
