"""Inventory of the online replay archive (results/online/<version>/<group>/group.zip): matches per version and per
opponent-rank bucket, for imitation data.

  python -B -X utf8 bots/main/long/train/archive.py
"""
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tracks as T  # noqa: E402

reg = json.loads((T.ROOT / 'results/online/groups.json').read_text(encoding='utf-8'))
versions = sorted({g['version'] for g in reg}, key=lambda v: int(v) if v.isdigit() else 0)
ms = list(T.matches(set(versions)))
by = defaultdict(Counter)
for m in ms:
    r = m['rank'] or 999
    by[m['version']]['1-5' if r <= 5 else '6-20' if r <= 20 else '21+']+= 1
tot = Counter()
for v in versions:
    if by[v]:
        print(v, dict(by[v]))
        tot.update(by[v])
print('total', dict(tot), sum(tot.values()))
