"""Paired per-game metrics of two subjects in an arena experiment: mean difference and its standard error for
wins, kill-death margin, deaths, kills, gold, damage - to see which outcome a change moves most clearly.

  python -B -X utf8 bots/main/long/train/paired.py EXP_NAME BASE OTHER   (EXP_NAME as in .cache/main/exp/)
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def main() -> None:
    name, base, other = sys.argv[1:4]
    rows = [json.loads(l) for l in (ROOT / '.cache/main/exp' / name / 'rows.jsonl').open(encoding='utf-8')]
    by = {}
    for r in rows:
        by.setdefault((r['map'], r['side'], r['opponent']), {})[r['subject']] = r
    metrics = {
        'win': lambda r: 1.0 if r['result'] == 'win' else (0.5 if r['result'] == 'draw' else 0.0),
        'kd': lambda r: r['kills'] - r['deaths'],
        'deaths': lambda r: r['deaths'],
        'kills': lambda r: r['kills'],
        'gold/100': lambda r: r['gold'] / 100.0,
        'opp_gold/100': lambda r: r['opp_gold'] / 100.0,
        'champ_dmg/1k': lambda r: (r['champion_damage'] - r['opp_champion_damage']) / 1000.0,
    }
    pairs = [v for v in by.values() if base in v and other in v]
    print(f'{len(pairs)} paired games')
    for k, f in metrics.items():
        d = [f(p[other]) - f(p[base]) for p in pairs]
        m = sum(d) / len(d)
        se = math.sqrt(sum((x - m) ** 2 for x in d) / max(1, len(d) - 1) / len(d))
        print(f'  {k:14s} {m:+8.3f} +- {se:.3f}  (t {m / se if se else 0:+.1f})')


if __name__ == '__main__':
    main()
