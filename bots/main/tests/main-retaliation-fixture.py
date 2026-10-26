"""Fixture: no actions after dying to retaliation during our own turn.

Regression for 2026-10-22 (archetype gauntlet): Ren's passive retaliates when damaged, so a
low-health champion that strikes a Focus-built Ren can die inside its own turn; the turn-end ward
placement then acted for a dead champion and the engine threw ("ward failed ... engine error 5",
3 of 640 gauntlet games, all against the burst persona). Plays release games against
anchors/main-v33 with the burst persona's knobs (Focus builds, draft mode 16 with Ren) on 01hooks
(team B) and 01gold_rush (both sides), and fails on any logged error line on our side.

Usage: python -B bots/main/tests/main-retaliation-fixture.py [--bot main/<bot>] [--keep]
       (--bot anchors/main-v33, the unfixed sources, shows the failure)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixturelib  # noqa: E402

OPPONENT = "anchors/main-v33|focus_builds=true,draft_mode=16"
GAMES = [("01hooks", "b", OPPONENT), ("01gold_rush", "a", OPPONENT), ("01gold_rush", "b", OPPONENT)]


def main() -> int:
    args = fixturelib.options(__doc__)
    games = fixturelib.play("retaliation", args.bot, GAMES)
    failures = 0
    for g in games or []:
        bad = [line.strip() for line in g.stdout.splitlines() if "error" in line.lower()]
        failures += len(bad)
        print(f"{g.map} side {g.side}: {len(bad)} error lines; {g.health()}" + (f"; first: {bad[0][:100]}" if bad else ""))
    return fixturelib.finish("retaliation", args.bot, games, failures == 0, args.keep)


if __name__ == "__main__":
    sys.exit(main())
