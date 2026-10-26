# What ends each walking stretch (activity log, debug_level=3). Edit the run path inside.
import json, re, zipfile, collections, statistics, sys
from pathlib import Path
ROOT = Path('.')
LINE = re.compile(rb"A(\d) (\d+) ([A-Za-z?]+)")
ends = collections.Counter(); lens = collections.defaultdict(list)
for line in open('.cache/contenders/compound/sr1/games.jsonl'):
    row = json.loads(line); side = row['tags']['subject']
    with zipfile.ZipFile(ROOT / row['replay']) as z:
        data = z.read(f"team-{side.lower()}.stdout")
    per = collections.defaultdict(dict)
    for m in LINE.finditer(data):
        slot, end, chars = int(m.group(1)), int(m.group(2)), m.group(3).decode()
        for i, c in enumerate(chars): per[slot][end - len(chars) + 1 + i] = c
    for slot, seq in per.items():
        s = ''.join(seq[r] for r in sorted(seq) if r <= 400)
        for m in re.finditer(r'W+', s):
            nxt = s[m.end()] if m.end() < len(s) else '$'
            ends[nxt] += 1; lens[nxt].append(len(m.group()))
tot = sum(ends.values())
for k, n in ends.most_common():
    print(f"{k} {n:5d} {100*n/tot:5.1f}%  mean len {statistics.mean(lens[k]):5.1f}  total {sum(lens[k]):6d}")
