"""Print a team's stdout from probe replays (*.repl26a zips) under a run directory.

  python -I probe_log.py RUN_DIR [--team a|b] [--grep REGEX] [--rounds A:B]
"""
from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("run")
    p.add_argument("--team", default="a")
    p.add_argument("--grep", default="")
    p.add_argument("--rounds", default="")
    a = p.parse_args()
    rx = re.compile(a.grep) if a.grep else None
    lo, hi = (int(x) for x in a.rounds.split(":")) if a.rounds else (0, 1 << 30)
    rr = re.compile(r"\b[Rr](\d+)\b")
    for rep in sorted(Path(a.run).rglob("*.repl26a")):
        with zipfile.ZipFile(rep) as z:
            name = f"team-{a.team}.stdout"
            if name not in z.namelist():
                continue
            text = z.read(name).decode("utf-8", "replace").replace("\x00", "")
        print(f"== {rep.name}")
        for line in text.splitlines():
            line = line.strip()
            if not line or (rx and not rx.search(line)):
                continue
            m = rr.search(line)
            if m and not (lo <= int(m.group(1)) <= hi):
                continue
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
