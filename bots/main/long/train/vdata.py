"""S2 value-network data: champion-view states ("S" lines, cfg::vlog, every 20 rounds) labelled with the game result,
from games on freshly generated maps against a league.

  python -B -X utf8 bots/main/long/train/vdata.py --out NAME [--maps 200] [--seed 21]
         [--vs anchors/main-v44,anchors/main-hunter7,anchors/starter-cpp,miscellaneous/compound] [--knobs K=V,...]

Writes .cache/main/long/data/NAME.npz: X (states, FIELDS), won (1/0.5/0), margin (final gem margin), game (id), map (id).
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import opt  # noqa: E402  (sets up tools/arena on the path)
import exp  # noqa: E402
import episodes as E  # noqa: E402
import qdata  # noqa: E402

FIELDS = ['round', 'self', 'type', 'banked_us', 'banked_them', 'gold', 'gems', 'hp', 'max_hp', 'strength', 'focus',
          'allies_alive', 'enemies_alive', 'allies_seen', 'enemies_seen', 'nearest_enemy', 'home_dist', 'deaths']


def parse(text: str) -> list[list[float]]:
    out = []
    for line in text.splitlines():
        p = line.split()
        if len(p) == len(FIELDS) + 1 and p[0] == 'S':
            try:
                out.append([float(v) for v in p[1:]])
            except ValueError:
                pass
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--maps', type=int, default=200)
    ap.add_argument('--seed', type=int, default=21)
    ap.add_argument('--vs', default='anchors/main-v44,anchors/main-hunter7,anchors/starter-cpp,miscellaneous/compound')
    ap.add_argument('--knobs', default='')
    ap.add_argument('--chunk', type=int, default=200)
    a = ap.parse_args()
    knobs = dict(kv.split('=', 1) for kv in a.knobs.split(',') if kv)
    knobs['vlog'] = '1'
    bots = {'s': opt.build('main/long', knobs)}
    opps = []
    for j, sel in enumerate(s for s in a.vs.split(',') if s):
        bots[f'o{j}'] = opt.build(sel, {})
        opps.append(f'o{j}')
    tmp = E.DATA / f'{a.out}.tmp'
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    maps = qdata.genmaps(a.maps, a.seed, tmp / 'maps')
    lines = [f'{m} {side} s - {o} - g.{o}' for o in opps for m in maps for side in 'AB']
    X, won, margin, game, mapid = [], [], [], [], []
    mid = {Path(m).stem: i for i, m in enumerate(maps)}
    gid = 0
    for c in range(0, len(lines), a.chunk):
        shutil.rmtree(tmp / 'b', ignore_errors=True)
        shutil.rmtree(tmp / 'run', ignore_errors=True)
        rows = exp.run_games(lines[c:c + a.chunk], bots, tmp / 'run', bundles=tmp / 'b')
        for r in rows:
            d = tmp / 'b' / f'{Path(r["map"]).stem}_{r["a_side"]}_{r["tag"]}'
            f = d / ('team-a.stdout' if r['a_side'] == 'A' else 'team-b.stdout')
            if not f.is_file():
                continue
            i = 0 if r['a_side'] == 'A' else 1
            w = 1.0 if r['winner'] == 'a' else (0.5 if r['winner'] == 'draw' else 0.0)
            mg = r['gems'][i] - r['gems'][1 - i]
            for s in parse(f.read_bytes().replace(b'\x00', b'').decode('utf-8', 'replace')):
                X.append(s); won.append(w); margin.append(mg); game.append(gid); mapid.append(mid[Path(r['map']).stem])
            gid += 1
        print(f'{min(c + a.chunk, len(lines))}/{len(lines)} games, {len(X)} states', flush=True)
    np.savez_compressed(E.DATA / f'{a.out}.npz', X=np.array(X, np.float32), won=np.array(won, np.float32),
                        margin=np.array(margin, np.float32), game=np.array(game), map=np.array(mapid), fields=np.array(FIELDS))
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    main()
