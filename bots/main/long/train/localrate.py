"""Per-type farm rates in local games (E1 follow-up): replays games of an arena experiment whose subject has
econlog=1 (arena exp replay), reads the subject's "E round index type alive gold gems" lines and prints gems/gold per
1000 alive rounds by champion type (the measure typerate.py uses for online games).

  python -B -X utf8 bots/main/long/train/localrate.py EXP SUBJECT OPPONENT MAP[,MAP...] [--round 300]
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
TYPES = {0: 'ryth', 1: 'vesper', 2: 'vendetta', 3: 'cassi', 4: 'issac', 5: 'stellar', 6: 'prism', 7: 'marcella',
         8: 'willow', 9: 'ren', 10: 'varg', 11: 'tavi', 12: 'heart'}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('exp')
    ap.add_argument('subject')
    ap.add_argument('opponent')
    ap.add_argument('maps')
    ap.add_argument('--round', type=int, default=300)
    a = ap.parse_args()
    owner, name = a.exp.split('/')
    agg = defaultdict(lambda: [0] * 8)
    rep = ROOT / '.cache' / owner / 'exp' / name / 'replays'
    for m in a.maps.split(','):
        for side in ('A', 'B'):
            r = subprocess.run([str(ROOT / 'arena.cmd'), 'exp', 'replay', a.exp, m, side, a.subject, a.opponent],
                               capture_output=True, text=True, cwd=ROOT)
            found = sorted(rep.glob(f'{m}_{side}_*'), key=lambda p: p.stat().st_mtime)
            if r.returncode != 0 or not found:
                print(m, side, 'replay failed', r.stderr[-300:])
                continue
            text = (found[-1] / f'team-{side.lower()}.stdout').read_bytes().replace(b'\x00', b'').decode('utf-8', 'replace')
            shutil.rmtree(found[-1], ignore_errors=True)
            for line in text.splitlines():
                p = line.split()
                if len(p) == 11 and p[0] == 'E' and int(p[1]) == a.round:
                    v = agg[int(p[3])]
                    v[0] += 1; v[1] += int(p[4]); v[2] += int(p[6]); v[3] += int(p[5])
                    for i in range(4):
                        v[4 + i] += int(p[7 + i])
    print(f'{a.subject} vs {a.opponent}, up to round {a.round}; camp = turns in fight_camps per alive turn, '
          f'then shares of those turns: hold (ranged waits for a tank), bad (ranged moves off a targeted tile), attacked')
    for k in sorted(agg):
        n, alive, gems, gold, camp, hold, bad, hit = agg[k]
        print(f'  {TYPES.get(k, k):9s} n {n:3d} alive {alive / n:5.0f} gems/1k {1000 * gems / max(1, alive):6.1f} '
              f'gold/1k {1000 * gold / max(1, alive):6.0f}  camp {camp / max(1, alive):.2f} hold {hold / max(1, camp):.2f} '
              f'bad {bad / max(1, camp):.2f} attacked {hit / max(1, camp):.2f}')


if __name__ == '__main__':
    main()
