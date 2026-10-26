"""Fixture: no approach/travel oscillation at the 01broken-curve wall corner.

Regression for 2026-10-21: at (5,30) the camp group is in view and the fight tile score (straight
line only) preferred (6,31), across a wall; there the camp is hidden and the travel step followed
the route back to (5,30). The champion swapped tiles every turn until the 25-round fight watchdog
banned the group (60 watchdog events in 8 broken-curve games). Fixed by counting the walking
distance in the out-of-reach tile score (cfg::fight_route_scoring, D70).

Plays the bot under test on 01broken-curve from both sides against anchors/main-v24 and fails if
a fight watchdog fires at (5,30).

Usage: python -B bots/main/tests/main-watchdog-fixture.py [--bot main/<bot>] [--keep]
       (--bot anchors/main-v24, the unfixed sources, fails)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixturelib  # noqa: E402

OPPONENT = "anchors/main-v24"
GAMES = [("01broken-curve", "a", OPPONENT), ("01broken-curve", "b", OPPONENT)]


def main() -> int:
    args = fixturelib.options(__doc__)
    games = fixturelib.play("watchdog", args.bot, GAMES)
    hits = 0
    for g in games or []:
        n = sum(1 for line in g.stdout.splitlines() if line.startswith("fight watchdog") and line.endswith("at 5,30"))
        hits += n
        print(f"{g.map} side {g.side}: {n} watchdog events at (5,30); {g.health()}")
    return fixturelib.finish("watchdog", args.bot, games, hits == 0, args.keep)


if __name__ == "__main__":
    sys.exit(main())
