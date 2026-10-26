"""Print the fuel stream of a replay bundle: python fuel-stream.py REPLAY.repl26a [--team a|b] [--from N] [--to N]

team-{a,b}.fuel: 8-byte magic 'a62leuf\\0', 5-byte header, then little-endian u64: 8 budget values
(draft first/other, play first/other, twice), then the fuel used per own-team turn in order
(draft turns, then gameplay turns of every body in turn order). Also prints the statistics JSON
fuelExhausted events when a matching <replay stem>.json is next to the replay."""
from __future__ import annotations

import json
import struct
import sys
import zipfile
from pathlib import Path

MAGIC = b"a62leuf\0"


def main() -> int:
    args = sys.argv[1:]
    path = Path(args[0])
    team = args[args.index("--team") + 1] if "--team" in args else "a"
    lo = int(args[args.index("--from") + 1]) if "--from" in args else 0
    hi = int(args[args.index("--to") + 1]) if "--to" in args else 10**9
    with zipfile.ZipFile(path) as z:
        raw = z.read(f"team-{team}.fuel")
    assert raw[:8] == MAGIC, raw[:8]
    header = raw[8:13]
    n = (len(raw) - 13) // 8
    vals = struct.unpack_from(f"<{n}Q", raw, 13)
    print(f"header {header.hex()} budgets {list(vals[:8])} turns {n - 8}")
    for i, v in enumerate(vals[8:]):
        if lo <= i <= hi:
            print(f"turn#{i} used {v}")
    stats = path.with_suffix(".json")
    if stats.is_file():
        s = json.loads(stats.read_text(encoding="utf-8"))
        for g in s.get("events") or []:
            if g.get("kind") == "fuelExhausted" and g.get("entries"):
                print("fuelExhausted", json.dumps(g)[:1500])
        print("diagnostics", json.dumps(s.get("diagnostics"))[:600])
    return 0


if __name__ == "__main__":
    sys.exit(main())
