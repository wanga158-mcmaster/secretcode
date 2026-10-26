"""Fixture: no fuel stall beside an adjacent enemy spawn (00reverse-spiral).

Regression for 2026-10-21 (v28 candidate): on 00reverse-spiral the spawns touch. With the
starter bot resting on its spawn, our champions on ours ran the threat phase every turn at ~45K
fuel (two 15-round skirmish simulations: an O(n) focus scan per hit, an n x n arrival scan, and
the second "leave without the spawn bonus" simulation computed after the hold cap had run out),
so the farm phase never had its fuel: 0 camp clears in 2000 rounds, 3 losses to starter-cpp.
Fixed by the fast focus path, the pairwise arrival scan and computing the second simulation only
while a hold can still happen (D78). The candidate idled 459 of 500 turns with 1191 threat
spikes; the fixed build <= 11 idle turns and 5 spikes.

Plays the bot under test on 00reverse-spiral from both sides against anchors/starter-cpp and
fails if any champion spent more than 100 of its first 500 turns idle, or the threat phase
spiked over 40K fuel more than 50 times.

Usage: python -B bots/main/tests/main-spawn-fixture.py [--bot main/<bot>] [--keep]
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fixturelib  # noqa: E402

OPPONENT = "anchors/starter-cpp"
GAMES = [("00reverse-spiral", "a", OPPONENT), ("00reverse-spiral", "b", OPPONENT)]
TIME_USE = re.compile(r"^t r500 k(\d+) .* idle(\d+) await\d+$", re.M)


def main() -> int:
    args = fixturelib.options(__doc__)
    games = fixturelib.play("spawn", args.bot, GAMES)
    failed = False
    for g in games or []:
        idle = [int(m.group(2)) for m in TIME_USE.finditer(g.stdout)]
        spikes = sum(1 for line in g.stdout.splitlines() if line.startswith("spike threats"))
        bad = not idle or max(idle) > 100 or spikes > 50
        failed |= bad
        print(f"{g.map} side {g.side}: idle turns by round 500 {idle}, threat spikes {spikes}; {g.health()}"
              + (" FAIL" if bad else ""))
    return fixturelib.finish("spawn", args.bot, games, not failed, args.keep)


if __name__ == "__main__":
    sys.exit(main())
