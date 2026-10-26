"""Which events a replay's trace records (DESIGN.md open item 1): event kinds and one example of each.

  python -B -X utf8 bots/main/zero/tools/tracekinds.py REPLAY.zip [--to 300]
"""
import argparse
import json
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
NODE = r'C:\Users\aegir\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe'

ap = argparse.ArgumentParser()
ap.add_argument('replay')
ap.add_argument('--to', type=int, default=300)
a = ap.parse_args()
p = subprocess.run([NODE, '--disable-warning=ExperimentalWarning', str(ROOT / 'tools/inspect-strategy-replay.mjs'), '--replay', a.replay,
                    '--trace', '1', '--from-round', '1', '--to-round', str(a.to), '--teams', '0,1', '--format', 'jsonl'],
                   capture_output=True, text=True)
lines = p.stdout.splitlines()
kinds, example = Counter(), {}
for l in lines[1:]:
    e = json.loads(l)
    k = e.get('kind') or e.get('record') or e.get('type') or '?'
    kinds[k] += 1
    example.setdefault(k, l[:400])
print(len(lines) - 1, 'events;', p.stderr[-300:] if p.returncode else '')
for k, n in kinds.most_common():
    print(f'{n:7d} {k}: {example[k]}')
