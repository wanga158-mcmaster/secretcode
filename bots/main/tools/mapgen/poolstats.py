# Pool statistics the map generator is fitted to (maps/geometry.json): size, symmetry, draft count,
# terrain densities, spawn box, altars, camp groups (members, types, values, cooldowns).
#   python poolstats.py [--per-map] [--templates OUT.txt]
# Templates (one line per pool map, read by fastrun --genmap): id w h symmetry(0 horizontal, 1 vertical,
# 2 rotational180) draft wall bush hole spawn_w spawn_h altars, then one token per camp group: its
# member camp types as digits (0 wolf, 1 elk, 2 gem cluster, 3 gold cluster, 4 monolith, 5 golem).
import json, statistics, sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
g = json.loads((ROOT / 'maps/geometry.json').read_text(encoding='utf-8'))
per_map = '--per-map' in sys.argv
if '--templates' in sys.argv:
    out = Path(sys.argv[sys.argv.index('--templates') + 1])
    sym_code = {'HORIZONTAL': 0, 'VERTICAL': 1, 'ROTATIONAL180': 2}
    lines = []
    for m in g['maps']:
        w, h, t, a = m['width'], m['height'], m['terrain'], m['spawns']['teamA']
        groups = [''.join(str(x['typeId']) for x in grp['members']) for grp in m['groups']]
        # The spawn box's max corner is exclusive (the engine allows 9-16 tiles; pool boxes are 3x3-4x4).
        lines.append(' '.join(str(v) for v in [m['id'], w, h, sym_code[m['symmetry']], m['draftCount'],
                                               f"{len(t['wall']) / (w * h):.4f}", f"{len(t['bush']) / (w * h):.4f}", f"{len(t['hole']) / (w * h):.4f}",
                                               a['maxX'] - a['minX'], a['maxY'] - a['minY'], len(t['altar'])] + groups))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{len(lines)} templates -> {out}')
    sys.exit(0)
sizes, syms, drafts, dens, spawn, altars, ngroups, members, types, gems, gold, cool = ([] for _ in range(12))
type_counts = Counter()
for m in g['maps']:
    w, h = m['width'], m['height']
    t = m['terrain']
    sizes.append((w, h))
    syms.append(m['symmetry'])
    drafts.append(m['draftCount'])
    dens.append({k: len(t[k]) / (w * h) for k in ('wall', 'bush', 'hole')})
    a = m['spawns']['teamA']
    spawn.append((a['maxX'] - a['minX'] + 1, a['maxY'] - a['minY'] + 1, a['minX'] / w, a['minY'] / h))
    altars.append(len(t['altar']))
    ngroups.append(len(m['groups']))
    for grp in m['groups']:
        members.append(len(grp['members']))
        gems.append(grp['gemValue'])
        gold.append(grp['goldValue'])
        cool.append(grp['cooldown'])
        kinds = tuple(sorted(str(x.get('type', x.get('campType', '?'))) for x in grp['members']))
        type_counts[kinds] += 1
    if per_map:
        print(f"{m['id']:24s} {w}x{h} {m['symmetry']:13s} draft {m['draftCount']} groups {len(m['groups']):2d} altars {len(t['altar'])} "
              f"wall {dens[-1]['wall']:.2f} bush {dens[-1]['bush']:.2f} hole {dens[-1]['hole']:.2f} spawn {spawn[-1][:2]}")
print('maps', len(sizes), 'sizes', Counter(sizes).most_common(8))
print('symmetry', Counter(syms), 'draft', Counter(drafts))
for k in ('wall', 'bush', 'hole'):
    v = [d[k] for d in dens]
    print(f'{k}: mean {statistics.mean(v):.3f} min {min(v):.3f} max {max(v):.3f}')
print('spawn box', Counter((s[0], s[1]) for s in spawn), 'altars', Counter(altars))
print('groups per map', Counter(ngroups), 'members per group', Counter(members))
print('gem value', Counter(gems).most_common(10), 'gold value', Counter(gold).most_common(10))
print('cooldown', Counter(cool).most_common(6))
print('member type sets', type_counts.most_common(14))
print('member keys', list(g['maps'][0]['groups'][0]['members'][0].keys()))
