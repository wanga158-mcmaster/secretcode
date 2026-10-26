"""Champion tracks of our online games (all ranks) for the hazard model: every match of the chosen versions in
results/online is decoded once with tools/inspect-strategy-replay.mjs --actor all into .cache/main/long/tracks
(regenerable). Writes an index (key, version, our_team, opponent, rank, result) next to them.

  python -B -X utf8 bots/main/long/train/tracks.py [--versions 44,49] [--jobs 8] [--list]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import zipfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / '.cache/main/long/tracks'
EXTRACT = ROOT / '.cache/main/long/extract'
NODE = r'C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
INSPECT = str(ROOT / 'tools/inspect-strategy-replay.mjs')
OUR = '34b36bab'


def matches(versions):
    reg = json.loads((ROOT / 'results/online/groups.json').read_text(encoding='utf-8'))
    for g in reg:
        if g['version'] not in versions:
            continue
        zp = ROOT / 'results/online' / g['version'] / g['group'] / 'group.zip'
        if not zp.is_file():
            continue
        with zipfile.ZipFile(zp) as gz:
            names = {n.split('-', 1)[1][:-4]: n for n in gz.namelist() if n.endswith('.zip')}
        for m in (g.get('show') or {}).get('matches', []):
            if m['matchId'] not in names:
                continue
            us = [t for t in m['teams'] if t['teamId'].startswith(OUR)]
            them = [t for t in m['teams'] if not t['teamId'].startswith(OUR)]
            if not us or not them:
                continue
            yield dict(key=f"{g['group'][:8]}-{m['position']}", version=g['version'], map=m['map'],
                       our_team=0 if us[0]['side'] == 'a' else 1, opp=them[0]['name'],
                       rank=(g.get('created') or {}).get('rank'), result=us[0].get('result'),
                       zip=str(zp), member=names[m['matchId']])


def decode(m):
    out = OUT / f"{m['key']}.json"
    if out.is_file():
        return m['key'], True
    EXTRACT.mkdir(parents=True, exist_ok=True)
    mz = EXTRACT / f"{m['key']}.zip"
    with zipfile.ZipFile(m['zip']) as gz:
        mz.write_bytes(gz.read(m['member']))
    r = subprocess.run([NODE, '--disable-warning=ExperimentalWarning', INSPECT, '--replay', str(mz), '--actor', 'all'],
                       capture_output=True, timeout=900)
    mz.unlink()
    if r.returncode != 0:
        print(m['key'], 'failed', r.stderr.decode('utf-8', 'replace')[-300:], flush=True)
        return m['key'], False
    out.write_bytes(r.stdout)
    return m['key'], True


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--versions', default='44,49')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    ms = list(matches(set(a.versions.split(','))))
    print(len(ms), 'matches', Counter(m['version'] for m in ms), flush=True)
    if a.list:
        print(Counter((m['rank'] or 0) // 10 * 10 for m in ms))
        return
    OUT.mkdir(parents=True, exist_ok=True)
    done = 0
    ok = []
    with ThreadPoolExecutor(a.jobs) as ex:
        for key, good in ex.map(decode, ms):
            done += 1
            if good:
                ok.append(key)
            if done % 100 == 0:
                print(done, 'decoded', flush=True)
    idx = [{k: v for k, v in m.items() if k not in ('zip', 'member')} for m in ms if m['key'] in set(ok)]
    (OUT / 'index.json').write_text(json.dumps(idx), encoding='utf-8')
    print('index', len(idx))


if __name__ == '__main__':
    main()
