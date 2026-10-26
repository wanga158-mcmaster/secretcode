"""Time use of contenders/compound from its activity log (build with --set debug_level=3).

  python -B bots/contenders/compound/tools/timeuse.py RUN_DIR [--rounds 400] [--by map|champion|none]

Each champion prints one character per own turn ("A<slot> <round> <chars>" every 100 rounds).
Codes: D dead, R recalling, S healing on spawn, T tribute channel, P PvP action, E fleeing an
enemy, V answering a beacon, G recall to shop, H recall to heal, B walking to bank (or tribute
start), h heal-trip recall start, e heal-trip escape (out of camp reach first), I no target,
F camp fight, C capture, w waiting at a respawning group, W walking to the target, ? other.
Prints the share of champion-turns per code up to --rounds (default 400) and the whole game.
"""
from __future__ import annotations

import argparse
import json
import re
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
CODES = "DRSTPEVGHBheIFCwW?"
LINE = re.compile(rb"A(\d) (\d+) ([A-Za-z?]+)")


def read_logs(replay: Path, team: str) -> dict:
    with zipfile.ZipFile(replay) as z:
        name = f"team-{team.lower()}.stdout"
        if name not in z.namelist():
            return {}
        data = z.read(name)
    per: dict = defaultdict(dict)   # slot -> {round: code}
    for m in LINE.finditer(data):
        slot, end, chars = int(m.group(1)), int(m.group(2)), m.group(3).decode()
        start = end - len(chars) + 1
        for i, c in enumerate(chars):
            per[slot][start + i] = c
    return per


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("runs", nargs="+")
    p.add_argument("--rounds", type=int, default=400)
    p.add_argument("--by", default="none", choices=["map", "champion", "none"])
    p.add_argument("--subject", default="compound")
    a = p.parse_args()
    early: dict = defaultdict(Counter)
    full: dict = defaultdict(Counter)
    for run in a.runs:
        for line in (Path(run) / "games.jsonl").read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            side = row.get("tags", {}).get("subject") or ("A" if a.subject in row.get("a_name", "") else "B")
            rep = ROOT / row["replay"] if row.get("replay") else None
            if not rep or not rep.exists():
                continue
            per = read_logs(rep, side)
            for slot, seq in per.items():
                key = row["map"] if a.by == "map" else f"slot{slot}" if a.by == "champion" else "all"
                for rnd, c in seq.items():
                    full[key][c] += 1
                    if rnd <= a.rounds:
                        early[key][c] += 1
    for label, table in ((f"r1-{a.rounds}", early), ("game", full)):
        print(f"== {label} (share of champion-turns, %)")
        print(f"{'group':14s} {'turns':>7s} " + " ".join(f"{c:>4s}" for c in CODES))
        for key in sorted(table):
            cnt = table[key]
            tot = sum(cnt.values()) or 1
            print(f"{key[:14]:14s} {tot:7d} " + " ".join(f"{100.0 * cnt[c] / tot:4.0f}" for c in CODES))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
