"""Play probe games on lab maps (map files outside maps/) through arena's snapshot builds and
run-batch, then extract the probes' log lines from the replays.

    python run_probe.py BOT MAP[,MAP...] [--opp BOT] [--sides A|B|both] [--rounds N] [--out DIR] [--grep RE]

BOT/--opp are arena selectors (e.g. contenders/_probe-monsters). MAP is a .map26a path. Logs go to
<out>/<map>-<side>.{a,b}.log (team A / team B stdout); lines starting "P " are printed unless
--quiet. Games are deterministic per (bots, map, side), so a rerun with the same sources replays
the same game. Uses the machine-wide arena slot pool (no reservation)."""
from __future__ import annotations

import argparse
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ROOT / "tools" / "arena"))
from core import bot as find_bot, prepare_many  # noqa: E402
from games import GameSpec, play  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("bot")
    ap.add_argument("maps")
    ap.add_argument("--opp", default="")
    ap.add_argument("--sides", default="A")
    ap.add_argument("--rounds", type=int, default=0)
    ap.add_argument("--out", default="")
    ap.add_argument("--grep", default="")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    bot = find_bot(a.bot)
    opp = find_bot(a.opp) if a.opp else bot
    ents = prepare_many([(bot, {}, ""), (opp, {}, "")])
    for e in ents:
        if not e.ok:
            print(f"BUILD FAILED {e.bot.id}\n{e.log[-3000:]}")
            return 1
    subject, other = ents
    maps = [str(Path(m).resolve()) for m in a.maps.split(",") if m]
    sides = ["A", "B"] if a.sides == "both" else [a.sides]
    games = [GameSpec(*((subject, other) if s == "A" else (other, subject)), m, {"subject": s, "probe": Path(m).stem})
             for m in maps for s in sides]
    out = Path(a.out or ROOT / ".cache/contenders/mechanics/runs" / Path(maps[0]).stem).resolve()
    rows = play(games, out, rounds=a.rounds, keep_replays="all", quiet=True, resume=False)
    rx = re.compile(a.grep) if a.grep else None
    for g, r in zip(games, rows):
        tag = f"{Path(g.map).stem}-{g.tags['subject']}"
        rep = r.get("replay")
        print(f"== {tag}: winner={r.get('winner')} rounds={r.get('rounds')} error={(r.get('error') or '')[:300]} replay={rep}")
        if not rep:
            continue
        rp = Path(rep) if Path(rep).is_absolute() else ROOT / rep
        with zipfile.ZipFile(rp) as z:
            names = z.namelist()
            for t in ("a", "b"):
                n = f"team-{t}.stdout"
                if n not in names:
                    continue
                text = z.read(n).replace(b"\0", b"").decode("utf-8", "replace")
                (out / f"{tag}.{t}.log").write_text(text, encoding="utf-8")
                if a.quiet:
                    continue
                for line in text.splitlines():
                    if (line.startswith("P ") or "error" in line.lower()) and (not rx or rx.search(line)):
                        print(f"{t}| {line}")
    print(f"out {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
