"""Fixture: Willow's ally heal never fails (draft mode 14 puts Willow on 5-pick maps).

Regression for 2026-10-21: the turn-end support step healed allies seen at turn start, using a
stale position, so casts failed with "Target must be a visible unit" / "Ability out of range"
(33-52 logged errors per 216 games). Builds the bot under test with draft_mode = 14 (a snapshot
override), plays two release games on 5-pick maps against anchors/main-v18 (01fracture as team A,
00lanes as team B) and fails if any heal cast errors.

Usage: python -B bots/main/tests/main-heal-fixture.py [--bot main/<bot>] [--keep]
       (--bot anchors/main-v18, the unfixed sources, shows the failure)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixturelib  # noqa: E402

OPPONENT = "anchors/main-v18"
GAMES = [("01fracture", "a", OPPONENT), ("00lanes", "b", OPPONENT)]


def main() -> int:
    args = fixturelib.options(__doc__)
    games = fixturelib.play("heal", args.bot, GAMES, {"draft_mode": "14"})
    failures = 0
    for g in games or []:
        bad = [line for line in g.stdout.splitlines() if "heal failed" in line]
        failures += len(bad)
        print(f"{g.map} side {g.side}: {len(bad)} failed heal casts; {g.health()}"
              + (f"; first: {bad[0][:90]}" if bad else ""))
    return fixturelib.finish("heal", args.bot, games, failures == 0, args.keep)


if __name__ == "__main__":
    sys.exit(main())
