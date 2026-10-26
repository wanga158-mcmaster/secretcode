"""Convert decoded games (decode.py) plus map geometry into plain token files for tools/replay_obs.cpp.

  python -B -X utf8 bots/main/zero/tools/totext.py [--limit N]

Writes .cache/main/zero/text/<matchId>.txt:
  MAP w h            FLAGS <w*h hex digits: 1 wall 2 hole 4 bush 8 altar>
  SPAWN team n x y ...                  (team 0 = A; max corner exclusive in geometry)
  ALTARS n x y ...
  GROUP native gold gems cooldown n native x y type ...
  CHAMP actor team type
  S actor round x y hp gold gems        (x = -1 while dead)
  E round actor kind ...                (kinds: move x y | attack ekind eid hpb hpa x y | cast slot x y nh [ekind eid hpb hpa]...
                                         | shop maxhp n [item count]... | recall | tribute | end | capture group n [champ gold gems]...
                                         | banked gems)
  WIN winner RANKS ra rb
"""
from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
DEC = ROOT / '.cache/main/zero/decoded'
OUT = ROOT / '.cache/main/zero/text'


def num(v, default=-1):
    return default if v is None else (int(v) if float(v).is_integer() else round(float(v), 2))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    geo = {m['id']: m for m in json.loads((ROOT / 'maps/geometry.json').read_text(encoding='utf-8'))['maps']}
    OUT.mkdir(parents=True, exist_ok=True)
    files = sorted(DEC.glob('*.json.gz'))
    if a.limit:
        files = files[:a.limit]
    written = skipped = 0
    for f in files:
        out = OUT / (f.name[:-8] + '.txt')
        if out.is_file() or (OUT.parent / 'pack/games' / (f.name[:-8] + '.npz')).is_file():  # converted or packed
            continue
        d = json.load(gzip.open(f, 'rt', encoding='utf-8'))
        m = geo.get((d.get('map') or '').replace('-', '_'))
        if m is None:
            skipped += 1
            continue
        w, h = m['width'], m['height']
        flags = [0] * (w * h)
        for name, bit in (('wall', 1), ('hole', 2), ('bush', 4), ('altar', 8)):
            for t in m['terrain'][name]:
                flags[t] |= bit
        lines = [f'MAP {w} {h}', 'FLAGS ' + ''.join('0123456789abcdef'[v] for v in flags)]
        for team, key in ((0, 'teamA'), (1, 'teamB')):
            b = m['spawns'][key]
            tiles = [(x, y) for y in range(b['minY'], b['maxY']) for x in range(b['minX'], b['maxX'])]
            lines.append(f'SPAWN {team} {len(tiles)} ' + ' '.join(f'{x} {y}' for x, y in tiles))
        alt = m['terrain']['altar']
        lines.append(f'ALTARS {len(alt)} ' + ' '.join(f'{t % w} {t // w}' for t in alt))
        for g in m['groups']:
            mem = ' '.join(f"{x['nativeId']} {x['x']} {x['y']} {x['typeId']}" for x in g['members'])
            lines.append(f"GROUP {g['nativeId']} {g['goldValue']} {g['gemValue']} {g['cooldown']} {len(g['members'])} {mem}")
        for actor, t in d['tracks'].items():
            lines.append(f"CHAMP {actor} {t['team']} {t['type']}")
            for s in t['samples']:
                lines.append(f'S {actor} ' + ' '.join(str(num(v)) for v in s))
        for e in d['events']:
            r, actor, kind = e[0], e[1], e[2]
            if kind == 'move':
                lines.append(f'E {r} {actor} move {e[3]} {e[4]}')
            elif kind == 'attack':
                ek, eid = e[3]
                lines.append(f'E {r} {actor} attack {ek} {eid} {num(e[4])} {num(e[5])} {e[6]} {e[7]}')
            elif kind == 'cast':
                hits = ' '.join(f'{hk} {hid} {num(hb)} {num(ha)}' for (hk, hid), hb, ha in e[6])
                lines.append(f'E {r} {actor} cast {num(e[3])} {e[4]} {e[5]} {len(e[6])} {hits}'.rstrip())
            elif kind == 'shop':
                items = ' '.join(f'{i} {c}' for i, c in e[3])
                lines.append(f'E {r} {actor} shop {num(e[4])} {len(e[3])} {items}'.rstrip())
            elif kind in ('recall', 'tribute', 'end'):
                lines.append(f'E {r} {actor} {kind}')
            elif kind == 'capture':
                champs = ' '.join(f'{c} {num(g)} {num(m_)}' for c, g, m_ in e[4])
                lines.append(f'E {r} -1 capture {e[3]} {len(e[4])} {champs}'.rstrip())
            elif kind == 'banked':
                lines.append(f'E {r} {actor} banked {num(e[3], 0)}')
        ranks = d.get('ranks') or [-1, -1]
        lines.append(f"WIN {d['winner']} RANKS {ranks[0] if ranks else -1} {ranks[1] if len(ranks) > 1 else -1}")
        out.write_bytes(('\n'.join(lines) + '\n').encode('utf-8'))
        written += 1
    print(f'{written} written, {skipped} skipped (map not in geometry)')


if __name__ == '__main__':
    main()
