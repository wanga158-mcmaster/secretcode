"""Placebo control: an option against itself with an irrelevant fuel burn (K vs KbN) next to the option's
effect (K vs 0). If the placebo spread matches the effect spread, fork advantages are luck.

  python -B -X utf8 bots/main/long/train/chaos.py RUN
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyse import value  # noqa: E402

ROOT = Path(__file__).resolve().parents[4]


def main() -> None:
    recs = [json.loads(l) for l in (ROOT / '.cache/main/long/runs' / sys.argv[1] / 'data.jsonl').open(encoding='utf-8')]
    keys = list(recs[0]['win'])
    print(f'{len(recs)} decisions, options {keys}')
    for k in keys:
        if 'b' in k:
            continue
        burn = next((b for b in keys if b.startswith(k + 'b')), None)
        pairs = []
        for r in recs:
            v0, vk, vb = value(r['win']['0']), value(r['win'][k]), value(r['win'][burn]) if burn else None
            if None not in (v0, vk, vb):
                pairs.append((vk - v0, vb - vk))
        if not pairs:
            continue
        eff = np.clip(np.array([p[0] for p in pairs]), -100, 100)
        plc = np.clip(np.array([p[1] for p in pairs]), -100, 100)
        print(f'option {k}: effect (K-0) moved {np.mean(np.abs(eff) > 0.5):.2f}, mean|.| {np.abs(eff).mean():5.1f}, sd {eff.std():5.1f}; '
              f'placebo (KbN-K) moved {np.mean(np.abs(plc) > 0.5):.2f}, mean|.| {np.abs(plc).mean():5.1f}, sd {plc.std():5.1f}; '
              f'corr(effect, placebo-shifted effect) {np.corrcoef(eff, eff + plc)[0, 1]:.2f}  n {len(pairs)}')


if __name__ == '__main__':
    main()
