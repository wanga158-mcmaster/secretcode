"""Print one example of each requested trace event kind from a replay (field discovery for the decoder).

  python -B -X utf8 bots/main/zero/tools/events.py REPLAY.zip --kinds shop,capture,gems-submitted --from 1 --to 64
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
NODE = r'C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'
ap = argparse.ArgumentParser()
ap.add_argument('replay')
ap.add_argument('--kinds', default='shop,capture,gems-submitted,recall,tribute')
ap.add_argument('--from', dest='lo', type=int, default=1)
ap.add_argument('--to', dest='hi', type=int, default=64)
a = ap.parse_args()
p = subprocess.run([NODE, '--disable-warning=ExperimentalWarning', str(ROOT / 'tools/inspect-strategy-replay.mjs'), '--replay', a.replay,
                    '--trace', '1', '--from-round', str(a.lo), '--to-round', str(a.hi), '--teams', '0,1', '--format', 'jsonl'],
                   capture_output=True, text=True)
want = set(a.kinds.split(','))
seen = set()
for line in p.stdout.splitlines()[1:]:
    e = json.loads(line)
    k = e.get('kind')
    if k in want and k not in seen:
        seen.add(k)
        e.pop('nearby', None)
        e.pop('visibility', None)
        print(k, json.dumps(e)[:1500])
        print()
print('missing:', want - seen, p.stderr[-200:] if p.returncode else '')
