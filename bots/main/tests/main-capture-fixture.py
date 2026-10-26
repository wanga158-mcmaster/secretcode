"""Fixture: no capture-walk deadlock on 01gold-rush.

Regression for 2026-10-21: a cleared group waiting for capture with a free member tile sends the
champion along the group's field; go_capture returned true even when the step failed. Two of our
champions stood in each other's only way to their capture tiles beside the gold-rush monolith
from round ~150 to the end (1890 of 1991 turns idle in "farm/-"), with no watchdog on that path.
Fixed by giving the target up after cfg::capture_stall_turns turns without a step (the callers
ban it for a few rounds; D78). Before the fix (v27 and the v28 candidate) the two champions idled
390 and 374 of their first 500 turns; after it <= 7.

Plays the bot under test as team B on 01gold-rush against anchors/main-hunter6 and fails if any
champion spent more than 100 of its first 500 turns idle.

Usage: python -B bots/main/tests/main-capture-fixture.py [--bot main/<bot>] [--keep]
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixturelib  # noqa: E402

OPPONENT = "anchors/main-hunter6"
GAMES = [("01gold-rush", "b", OPPONENT)]
TIME_USE = re.compile(r"^t r500 k(\d+) .* idle(\d+) await\d+$", re.M)


def main() -> int:
    args = fixturelib.options(__doc__)
    games = fixturelib.play("capture", args.bot, GAMES)
    failed = False
    for g in games or []:
        idle = [int(m.group(2)) for m in TIME_USE.finditer(g.stdout)]
        stalls = sum(1 for line in g.stdout.splitlines() if line.startswith("capture stall"))
        bad = not idle or max(idle) > 100
        failed |= bad
        print(f"{g.map} side {g.side}: idle turns by round 500 {idle}, capture stalls given up {stalls}; {g.health()}"
              + (" FAIL" if bad else ""))
    return fixturelib.finish("capture", args.bot, games, not failed, args.keep)


if __name__ == "__main__":
    sys.exit(main())
